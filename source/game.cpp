#include "game.h"
#include <vector>
#include <string>
#include <math.h>
#include <assert.h>
#include <algorithm>

#define INLINE __forceinline

const int kObjectCount = 1000000;
const int kAvoidCount = 20;

static float RandomFloat01() { return (float)rand() / (float)RAND_MAX; }
static float RandomFloat(float from, float to) { return RandomFloat01() * (to - from) + from; }
static float RandomAngle() { return RandomFloat01() * 3.1415926f * 2; }
float Clamp( float x, float lower, float upper ) { return std::max(lower, std::min(upper, x)); }

template<class T> struct Id { int index; operator size_t() const { return index; } };

struct Position;
struct Color;
struct Sprite;
struct Avoid;
struct AvoidThis;
template<class CRTP>
struct Allocators
{
	template<class T, class... Args> Id<T> New(Args&&... args);
};
struct MutableComponents : Allocators<MutableComponents>
{
	std::vector<Position> pos;
	std::vector<Color> color;

	void SizeHint(size_t size);
};
struct StaticComponents : Allocators<StaticComponents>
{
	std::vector<Sprite> sprite;
	std::vector<Avoid> avoid;
	std::vector<AvoidThis> avoidThis;

	void SizeHint(size_t size);
};
// -------------------------------------------------------------------------------------------------
// components we use in our "game"


// World bounds for our "game" logic: x,y minimum & maximum values
struct WorldBounds
{
    const float xMin, xMax, yMin, yMax;
	
    WorldBounds(float xMin, float xMax, float yMin, float yMax)
		: xMin(xMin)
		, xMax(xMax)
		, yMin(yMin)
		, yMax(yMax)
    {
	}
};

#define MUTABLE_ASSIGNABLE(T) T& operator=(const T& o) { this->~T(); return *new(this)T(o); }

// 2D position: just x,y coordinates
struct Position
{
    const float x, y;
    const float velx, vely;

    Position(const WorldBounds& bounds, float boundsScale, float angle, float speed)
		: x(RandomFloat(bounds.xMin, bounds.xMax) * boundsScale)
		, y(RandomFloat(bounds.yMin, bounds.yMax) * boundsScale)
        , velx(cosf(angle) * speed)
        , vely(sinf(angle) * speed)
    {
	}
    Position(float x, float y, float velx, float vely)
		: x(x)
		, y(y)
        , velx(velx)
        , vely(vely)
    {
	}

	MUTABLE_ASSIGNABLE(Position);
	
	// Move around with constant velocity. When reached world bounds, reflect back from them.
    INLINE Position UpdatePosition(float deltaTime, WorldBounds& bounds) const
    {
        // update position based on movement velocity & delta time
        float _x = x + velx * deltaTime;
        float _y = y + vely * deltaTime;
		
        // check against world bounds; put back onto bounds and mirror the velocity  to "bounce" back
		float xVelScale = _x < bounds.xMin ? -1.f : (_x > bounds.xMax ? -1.f : 1.f);
		float yVelScale = _y < bounds.yMin ? -1.f : (_y > bounds.yMax ? -1.f : 1.f);
		_x = Clamp(_x, bounds.xMin, bounds.xMax);
		_y = Clamp(_y, bounds.yMin, bounds.yMax);
		float _velx = velx * xVelScale;
		float _vely = vely * yVelScale;
		return { _x, _y, _velx, _vely };
    }

	static void UpdatePositions(float deltaTime, WorldBounds& bounds, const std::vector<Position>& inputs, std::vector<Position>& outputs)
	{
		for (size_t i=0, end=inputs.size(); i!=end; ++i)
		{
			outputs[i] = inputs[i].UpdatePosition(deltaTime, bounds);
		}
	}
};


struct Color
{
    const float colorR, colorG, colorB;
	
    Color(float colorR, float colorG, float colorB)
		: colorR(colorR)
		, colorG(colorG)
		, colorB(colorB)
    {
	}

	MUTABLE_ASSIGNABLE(Color);
};

// Sprite: sprite index (in the sprite atlas), and scale for rendering it
struct Sprite
{
    const float scale;
    const int spriteIndex;
	const Id<Color> colorId;
	const Id<Position> posId;
	
    Sprite(int spriteIndex, float scale, Id<Color> color, Id<Position> pos)
		: scale(scale)
		, spriteIndex(spriteIndex)
		, colorId(color)
		, posId(pos)
    {
	}
};


// When present, tells things that have Avoid to avoid this object
struct AvoidThis
{
    float distanceSq;
	Id<Color> colorId;
	Id<Position> posId;
	
    AvoidThis(float distance, Id<Color> color, Id<Position> position)
		: distanceSq(distance * distance)
		, colorId(color)
		, posId(position)
    {
	}
};

// Objects with this component "avoid" objects with AvoidThis component:
// - when they get closer to them than Avoid::distance, they bounce back,
// - also they take sprite color from the object they just bumped into
struct Avoid
{
	Id<Position> posId;
	Id<Color> colorId;
	
    Avoid(Id<Position> pos, Id<Color> color)
		: posId(pos)
		, colorId(color)
    {
	}

    static float DistanceSq(const Position& a, const Position& b)
    {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }
    
    static Position ResolveCollision(float deltaTime, const Position& pos)
    {
        // flip velocity
        float velx = -pos.velx;
        float vely = -pos.vely;
        
        // move us out of collision, by moving just a tiny bit more than we'd normally move during a frame
        float x = pos.x + velx * deltaTime * 1.1f;
        float y = pos.y + vely * deltaTime * 1.1f;
		return {x, y, velx, vely};
    }
	
	struct NewState
	{
		Position pos;
		Color color;
	};
	
    INLINE NewState ResolveCollisions(float deltaTime, const std::vector<Position>& positions, const std::vector<Color>& colors, const std::vector<AvoidThis>& avoidThese) const
    {
		const Position& myPos = positions[posId];
		const Color& myColor = colors[colorId];
        // check each thing in avoid list
        for (auto& o : avoidThese)
        {
			const Color& oColor = colors[o.colorId];
			const Position& oPos = positions[o.posId];

            // is our position closer to "thing to avoid" position than the avoid distance?
            if (DistanceSq(myPos, oPos) < o.distanceSq)
            {
                Position newPos = ResolveCollision(deltaTime, myPos);
                // also make our sprite take the color of the thing we just bumped into
				return { newPos, oColor };
            }
        }
		return { myPos, myColor };
    }
	
	static void ResolveCollisions(float deltaTime, std::vector<Position>& out_pos, std::vector<Color>& out_color, const std::vector<Position>& in_pos, const std::vector<Color>& in_color, const std::vector<Avoid>& avoid, const std::vector<AvoidThis>& in_avoidThese)
	{
		for (size_t i=0, end=avoid.size(); i!=end; ++i)
		{
			const Avoid& c = avoid[i];
			const auto& result = c.ResolveCollisions(deltaTime, in_pos, in_color, in_avoidThese);
			out_pos[c.posId] = result.pos;
			out_color[c.colorId] = result.color;
		}
	}
};

template<class T> struct GetAllocatorInternal { static std::vector<T>& Vector(...); };
template<> struct GetAllocatorInternal<Position>  { static std::vector<Position>&  Vector(MutableComponents& a) { return a.pos; } };
template<> struct GetAllocatorInternal<Color>     { static std::vector<Color>&     Vector(MutableComponents& a) { return a.color; } };
template<> struct GetAllocatorInternal<Sprite>    { static std::vector<Sprite>&    Vector(StaticComponents& a) { return a.sprite; } };
template<> struct GetAllocatorInternal<Avoid>     { static std::vector<Avoid>&     Vector(StaticComponents& a) { return a.avoid; } };
template<> struct GetAllocatorInternal<AvoidThis> { static std::vector<AvoidThis>& Vector(StaticComponents& a) { return a.avoidThis; } };

template<class A>
template<class T, class... Args>
Id<T> Allocators<A>::New(Args&&... args)
{
	std::vector<T>& storage = GetAllocatorInternal<T>::Vector((A&)*this);
	int index = (int)storage.size();
	storage.emplace_back(std::forward<Args>(args)...);
	return { index };
}
void MutableComponents::SizeHint(size_t size)
{
	pos.reserve(size);
	color.reserve(size);
}
void StaticComponents::SizeHint(size_t size)
{
	sprite.reserve(size);
	avoid.reserve(size);
	avoidThis.reserve(size);
}

void NewAvoidThis(MutableComponents& m, StaticComponents& s, const WorldBounds& bounds)
{
    // random angle
    // random movement speed between given min & max
	// position it in small area near center of world bounds
	Id<Position> pos = m.New<Position>(
		bounds, 0.2f,
		RandomAngle(), RandomFloat(0.1f, 0.2f));
    // setup a sprite for it (6th one), and a random color
	Id<Color> color = m.New<Color>(
		RandomFloat(0.5f, 1.0f),
		RandomFloat(0.5f, 1.0f),
		RandomFloat(0.5f, 1.0f));
	s.New<Sprite>(
		5,
		2.0f,
		color,
		pos);
        // setup an "avoid this" component
	s.New<AvoidThis>(1.3f, color, pos);
}

void NewRegularObject(MutableComponents& m, StaticComponents& s, const WorldBounds& bounds)
{
	// position it within world bounds
	Id<Position> pos = m.New<Position>(
		bounds, 1.0f,
		RandomAngle(), RandomFloat(0.5f, 0.7f));
        // setup a sprite for it (random sprite index from first 5), and initial white color
	Id<Color> color = m.New<Color>(1.f, 1.f, 1.f);
	s.New<Sprite>(
		rand() % 5,
		1.0f,
		color,
		pos);
	s.New<Avoid>(pos, color);
}

// -------------------------------------------------------------------------------------------------
// "the game"

struct Game
{
	WorldBounds bounds;
	MutableComponents mComponents;
	MutableComponents mComponentsBuffer;
	StaticComponents  sComponents;

	Game(const WorldBounds& bounds)
		: bounds(bounds)
	{
		mComponents.SizeHint(kObjectCount + kAvoidCount);
		sComponents.SizeHint(kObjectCount + kAvoidCount);

		// create regular objects that move
		for (auto i = 0; i < kObjectCount; ++i)
			NewRegularObject(mComponents, sComponents, bounds);

		// create objects that should be avoided
		for (auto i = 0; i < kAvoidCount; ++i)
			NewAvoidThis(mComponents, sComponents, bounds);
		
		mComponentsBuffer = mComponents;
	}
};

int ExportSpriteData(const std::vector<Sprite>& sprites, const std::vector<Position>& positions, const std::vector<Color>& colors, sprite_data_t* out)
{
	int objectCount = 0;
    for (auto& sprite : sprites)
	{
		sprite_data_t& spr = out[objectCount++];
		const Position& pos = positions[sprite.posId];
		const Color& color = colors[sprite.colorId];
		// write out their Position & Sprite data into destination buffer that will be rendered later on.
		//
		// Using a smaller global scale "zooms out" the rendering, so to speak.
		float globalScale = 0.05f;
		spr.posX = pos.x * globalScale;
		spr.posY = pos.y * globalScale;
		spr.scale = sprite.scale * globalScale;
		spr.colR = color.colorR;
		spr.colG = color.colorG;
		spr.colB = color.colorB;
		spr.sprite = (float)sprite.spriteIndex;
    }
	return objectCount;
}


static Game* s_game = 0;

extern "C" void game_initialize(void)
{
	s_game = new Game({-80.0f, 80.0f, -50.0f, 50.0f });
}

extern "C" void game_destroy(void)
{
    delete s_game;
}

extern "C" int game_update(sprite_data_t* data, double time, float deltaTime)
{
	assert(s_game);
	const StaticComponents& constants = s_game->sComponents;
	MutableComponents& state = s_game->mComponents;
	MutableComponents& buffer = s_game->mComponentsBuffer;
	
	std::vector<Position> movedPositions;
	std::swap(movedPositions, buffer.pos);


    // Update all positions
	Position::UpdatePositions(deltaTime, s_game->bounds, state.pos, movedPositions);
	
	std::vector<Position> resolvedPositions;
	std::swap(resolvedPositions, state.pos);
	std::vector<Color> resolvedColors;
	std::swap(resolvedColors, buffer.color);

    // Resolve all collisions
	Avoid::ResolveCollisions(deltaTime, resolvedPositions, resolvedColors, movedPositions, state.color, constants.avoid, constants.avoidThis);
	
	std::swap(buffer.pos, movedPositions);
	std::swap(buffer.color, state.color);
	
	std::swap(state.pos, resolvedPositions);
	std::swap(state.color, resolvedColors);

    return ExportSpriteData(constants.sprite, state.pos, state.color, data);
}

