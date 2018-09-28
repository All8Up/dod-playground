#include "game.h"
#include <vector>
#include <string>
#include <math.h>
#include <assert.h>
#include <algorithm>

const int kObjectCount = 1000000;
const int kAvoidCount = 20;



static float RandomFloat01() { return (float)rand() / (float)RAND_MAX; }
static float RandomFloat(float from, float to) { return RandomFloat01() * (to - from) + from; }


template<class T> struct Id { int index; };

struct PositionComponent;
struct ColorComponent;
struct SpriteComponent;
struct MoveComponent;
struct AvoidComponent;
struct AvoidThisComponent;
struct Allocators
{
	std::vector<PositionComponent> pos;
	std::vector<ColorComponent> color;
	std::vector<SpriteComponent> sprite;
	std::vector<MoveComponent> move;
	std::vector<AvoidComponent> avoid;
	std::vector<AvoidThisComponent> avoidThis;

	template<class T, class... Args>
	Id<T> New(Args&&... args);

	void SizeHint(size_t size);
	
	template<class T>       T& operator[]( Id<T> i );
	template<class T> const T& operator[]( Id<T> i ) const;
};
// -------------------------------------------------------------------------------------------------
// components we use in our "game"


// 2D position: just x,y coordinates
struct PositionComponent
{
    float x, y;
	
    PositionComponent(float x, float y)
		: x(x)
		, y(y)
    {
	}
};


struct ColorComponent
{
    float colorR, colorG, colorB;

    ColorComponent(float colorR, float colorG, float colorB)
		: colorR(colorR)
		, colorG(colorG)
		, colorB(colorB)
    {
	}
};

// Sprite: sprite index (in the sprite atlas), and scale for rendering it
struct SpriteComponent
{
    float scale;
    int spriteIndex;
	Id<ColorComponent> colorId;
	Id<PositionComponent> posId;
	
    SpriteComponent(int spriteIndex, float scale, Id<ColorComponent> color, Id<PositionComponent> pos)
		: scale(scale)
		, spriteIndex(spriteIndex)
		, colorId(color)
		, posId(pos)
    {
	}
};


// World bounds for our "game" logic: x,y minimum & maximum values
struct WorldBoundsComponent
{
    float xMin, xMax, yMin, yMax;
	
    WorldBoundsComponent(float xMin, float xMax, float yMin, float yMax)
		: xMin(xMin)
		, xMax(xMax)
		, yMin(yMin)
		, yMax(yMax)
    {
	}
};

float Clamp( float x, float lower, float upper )
{
	return std::max(lower, std::min(upper, x));
}

// Move around with constant velocity. When reached world bounds, reflect back from them.
struct MoveComponent
{
    float velx, vely;
	Id<PositionComponent> posId;
    
    MoveComponent(float minSpeed, float maxSpeed, Id<PositionComponent> pos)
		: posId(pos)
    {
        // random angle
        float angle = RandomFloat01() * 3.1415926f * 2;
        // random movement speed between given min & max
        float speed = RandomFloat(minSpeed, maxSpeed);
        // velocity x & y components
        velx = cosf(angle) * speed;
        vely = sinf(angle) * speed;
    }

    void UpdatePosition(float deltaTime, WorldBoundsComponent& bounds, Allocators& a)
    {
		PositionComponent& pos = a[posId];
        // update position based on movement velocity & delta time
        pos.x += velx * deltaTime;
        pos.y += vely * deltaTime;
		
        // check against world bounds; put back onto bounds and mirror the velocity component to "bounce" back
		float xVelScale = pos.x < bounds.xMin ? -1.f : (pos.x > bounds.xMax ? -1.f : 1.f);
		float yVelScale = pos.y < bounds.yMin ? -1.f : (pos.y > bounds.yMax ? -1.f : 1.f);
		pos.x = Clamp(pos.x, bounds.xMin, bounds.xMax);
		pos.y = Clamp(pos.y, bounds.yMin, bounds.yMax);
		velx *= xVelScale;
		vely *= yVelScale;
    }
};

// When present, tells things that have Avoid component to avoid this object
struct AvoidThisComponent
{
    float distanceSq;
	Id<ColorComponent> colorId;
	Id<PositionComponent> posId;
	
    AvoidThisComponent(float distance, Id<ColorComponent> color, Id<PositionComponent> position)
		: distanceSq(distance * distance)
		, colorId(color)
		, posId(position)
    {
	}
};

// Objects with this component "avoid" objects with AvoidThis component:
// - when they get closer to them than Avoid::distance, they bounce back,
// - also they take sprite color from the object they just bumped into
struct AvoidComponent
{
	Id<MoveComponent> moveId;
	Id<ColorComponent> colorId;
	
    AvoidComponent(Id<MoveComponent> move, Id<ColorComponent> color)
		: moveId(move)
		, colorId(color)
    {
	}

    static float DistanceSq(const PositionComponent& a, const PositionComponent& b)
    {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }
    
    static void ResolveCollision(float deltaTime, MoveComponent& move, PositionComponent& pos)
    {
        // flip velocity
        move.velx = -move.velx;
        move.vely = -move.vely;
        
        // move us out of collision, by moving just a tiny bit more than we'd normally move during a frame
        pos.x += move.velx * deltaTime * 1.1f;
        pos.y += move.vely * deltaTime * 1.1f;
    }
	
    void ResolveCollisions(float deltaTime, Allocators& a)
    {
		MoveComponent& myMove = a[moveId];
		ColorComponent& myColor = a[colorId];
		PositionComponent& myPos = a[myMove.posId];
        // check each thing in avoid list
        for (auto& o : a.avoidThis)
        {
			ColorComponent& oColor = a[o.colorId];
			PositionComponent& oPos = a[o.posId];
            // is our position closer to "thing to avoid" position than the avoid distance?
            if (DistanceSq(myPos, oPos) < o.distanceSq)
            {
                ResolveCollision(deltaTime, myMove, myPos);

                // also make our sprite take the color of the thing we just bumped into
                myColor.colorR = oColor.colorR;
                myColor.colorG = oColor.colorG;
                myColor.colorB = oColor.colorB;
            }
        }
    }
};

template<class T> struct GetAllocatorInternal { static std::vector<T>& Vector(Allocators&); };
template<> struct GetAllocatorInternal<PositionComponent>  { static std::vector<PositionComponent>&  Vector(Allocators& a) { return a.pos; } };
template<> struct GetAllocatorInternal<ColorComponent>     { static std::vector<ColorComponent>&     Vector(Allocators& a) { return a.color; } };
template<> struct GetAllocatorInternal<SpriteComponent>    { static std::vector<SpriteComponent>&    Vector(Allocators& a) { return a.sprite; } };
template<> struct GetAllocatorInternal<MoveComponent>      { static std::vector<MoveComponent>&      Vector(Allocators& a) { return a.move; } };
template<> struct GetAllocatorInternal<AvoidComponent>     { static std::vector<AvoidComponent>&     Vector(Allocators& a) { return a.avoid; } };
template<> struct GetAllocatorInternal<AvoidThisComponent> { static std::vector<AvoidThisComponent>& Vector(Allocators& a) { return a.avoidThis; } };

template<class T, class... Args>
Id<T> Allocators::New(Args&&... args)
{
	std::vector<T>& storage = GetAllocatorInternal<T>::Vector(*this);
	int index = (int)storage.size();
	storage.emplace_back(std::forward<Args>(args)...);
	return { index };
}
template<class T>       T& Allocators::operator[]( Id<T> i )       { std::vector<T>& v = GetAllocatorInternal<T>::Vector(*this); return v[i.index]; }
template<class T> const T& Allocators::operator[]( Id<T> i ) const { std::vector<T>& v = GetAllocatorInternal<T>::Vector(*this); return v[i.index]; }
void Allocators::SizeHint(size_t size)
{
	pos.reserve(size);
	sprite.reserve(size);
	move.reserve(size);
	avoid.reserve(size);
	avoidThis.reserve(size);
}

void NewAvoidThis(Allocators& a, const WorldBoundsComponent& bounds)
{
        // position it in small area near center of world bounds
	Id<PositionComponent> pos = a.New<PositionComponent>(
		RandomFloat(bounds.xMin, bounds.xMax) * 0.2f,
		RandomFloat(bounds.yMin, bounds.yMax) * 0.2f);
	Id<MoveComponent> move = a.New<MoveComponent>(0.1f, 0.2f, pos);
    // setup a sprite for it (6th one), and a random color
	Id<ColorComponent> color = a.New<ColorComponent>(
		RandomFloat(0.5f, 1.0f),
		RandomFloat(0.5f, 1.0f),
		RandomFloat(0.5f, 1.0f));
	a.New<SpriteComponent>(
		5,
		2.0f,
		color,
		pos);
        // setup an "avoid this" component
	a.New<AvoidThisComponent>(1.3f, color, pos);
}

void NewRegularObject(Allocators& a, const WorldBoundsComponent& bounds)
{
        // position it within world bounds
	Id<PositionComponent> pos = a.New<PositionComponent>(
		RandomFloat(bounds.xMin, bounds.xMax),
		RandomFloat(bounds.yMin, bounds.yMax));
	Id<MoveComponent> move = a.New<MoveComponent>(0.5f, 0.7f, pos);
        // setup a sprite for it (random sprite index from first 5), and initial white color
	Id<ColorComponent> color = a.New<ColorComponent>(1.f, 1.f, 1.f);
	a.New<SpriteComponent>(
		rand() % 5,
		1.0f,
		color,
		pos);
	a.New<AvoidComponent>(move, color);
}

// -------------------------------------------------------------------------------------------------
// "the game"

struct Game
{
	WorldBoundsComponent bounds;
	Allocators components;

	Game(const WorldBoundsComponent& bounds)
		: bounds(bounds)
	{
		components.SizeHint(kObjectCount + kAvoidCount);

		// create regular objects that move
		for (auto i = 0; i < kObjectCount; ++i)
			NewRegularObject(components, bounds);

		// create objects that should be avoided
		for (auto i = 0; i < kAvoidCount; ++i)
			NewAvoidThis(components, bounds);
	}
};


void ExportSpriteData(const SpriteComponent& sprite, sprite_data_t& spr, Allocators& a)
{
	PositionComponent& pos = a[sprite.posId];
	ColorComponent& color = a[sprite.colorId]
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
    // Update all positions
    for (auto& c : s_game->components.move)
	{
		c.UpdatePosition(deltaTime, s_game->bounds, s_game->components);
	}
	
    // Resolve all collisions
    for (auto& c : s_game->components.avoid)
	{
		c.ResolveCollisions(deltaTime, s_game->components);
    }

	int objectCount = 0;
    for (auto& c : s_game->components.sprite)
	{
		ExportSpriteData(c, data[objectCount++], s_game->components);
    }
    return objectCount;
}

