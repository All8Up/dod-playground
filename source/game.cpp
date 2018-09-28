#include "game.h"
#include <vector>
#include <string>
#include <math.h>
#include <assert.h>

const int kObjectCount = 1000000;
const int kAvoidCount = 20;



static float RandomFloat01() { return (float)rand() / (float)RAND_MAX; }
static float RandomFloat(float from, float to) { return RandomFloat01() * (to - from) + from; }

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


// Sprite: color, sprite index (in the sprite atlas), and scale for rendering it
struct SpriteComponent
{
    float colorR, colorG, colorB;
    int spriteIndex;
    float scale;
	PositionComponent& pos;
	
    SpriteComponent(float colorR, float colorG, float colorB, int spriteIndex, float scale, PositionComponent& pos)
		: colorR(colorR)
		, colorG(colorG)
		, colorB(colorB)
		, spriteIndex(spriteIndex)
		, scale(scale)
		, pos(pos)
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


// Move around with constant velocity. When reached world bounds, reflect back from them.
struct MoveComponent
{
    float velx, vely;
	PositionComponent& pos;
    
    MoveComponent(float minSpeed, float maxSpeed, PositionComponent& pos)
		: pos(pos)
    {
        // random angle
        float angle = RandomFloat01() * 3.1415926f * 2;
        // random movement speed between given min & max
        float speed = RandomFloat(minSpeed, maxSpeed);
        // velocity x & y components
        velx = cosf(angle) * speed;
        vely = sinf(angle) * speed;
    }

    void UpdatePosition(float deltaTime, WorldBoundsComponent& bounds)
    {
        // update position based on movement velocity & delta time
        pos.x += velx * deltaTime;
        pos.y += vely * deltaTime;
        
        // check against world bounds; put back onto bounds and mirror the velocity component to "bounce" back
        if (pos.x < bounds.xMin)
        {
            velx = -velx;
            pos.x = bounds.xMin;
        }
        if (pos.x > bounds.xMax)
        {
            velx = -velx;
            pos.x = bounds.xMax;
        }
        if (pos.y < bounds.yMin)
        {
            vely = -vely;
            pos.y = bounds.yMin;
        }
        if (pos.y > bounds.yMax)
        {
            vely = -vely;
            pos.y = bounds.yMax;
        }
    }
};

// When present, tells things that have Avoid component to avoid this object
struct AvoidThisComponent
{
    float distance;
	SpriteComponent& sprite;
	
    AvoidThisComponent(float distance, SpriteComponent& sprite)
		: distance(distance)
		, sprite(sprite)
    {
	}
};

// Objects with this component "avoid" objects with AvoidThis component:
// - when they get closer to them than Avoid::distance, they bounce back,
// - also they take sprite color from the object they just bumped into
struct AvoidComponent
{
	MoveComponent& myMove;
	SpriteComponent& mySprite;
	
    AvoidComponent(MoveComponent& move, SpriteComponent& sprite)
		: myMove(move)
		, mySprite(sprite)
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
	
    void ResolveCollisions(float deltaTime, std::vector<AvoidThisComponent>& avoidList)
    {
        // check each thing in avoid list
        for (auto& o : avoidList)
        {
            // is our position closer to "thing to avoid" position than the avoid distance?
            if (DistanceSq(myMove.pos, o.sprite.pos) < o.distance * o.distance)
            {
                ResolveCollision(deltaTime, myMove, myMove.pos);

                // also make our sprite take the color of the thing we just bumped into
                mySprite.colorR = o.sprite.colorR;
                mySprite.colorG = o.sprite.colorG;
                mySprite.colorB = o.sprite.colorB;
            }
        }
    }
};

struct Allocators;
template<class T> struct GetAllocatorInternal { static std::vector<T>& Vector(Allocators&); };
struct Allocators
{
	void SizeHint(size_t size)
	{
		pos.reserve(size);
		sprite.reserve(size);
		move.reserve(size);
		avoidThis.reserve(size);
	}
	std::vector<PositionComponent> pos;
	std::vector<SpriteComponent> sprite;
	std::vector<MoveComponent> move;
	std::vector<AvoidComponent> avoid;
	std::vector<AvoidThisComponent> avoidThis;

	template<class T, class... Args>
	T& New(Args&&... args)
	{
		std::vector<T>& storage = GetAllocatorInternal<T>::Vector(*this);
		storage.emplace_back(std::forward<Args>(args)...);
		return storage.back();
	}
};

template<> struct GetAllocatorInternal<PositionComponent>  { static std::vector<PositionComponent>&  Vector(Allocators& a) { return a.pos; } };
template<> struct GetAllocatorInternal<SpriteComponent>    { static std::vector<SpriteComponent>&    Vector(Allocators& a) { return a.sprite; } };
template<> struct GetAllocatorInternal<MoveComponent>      { static std::vector<MoveComponent>&      Vector(Allocators& a) { return a.move; } };
template<> struct GetAllocatorInternal<AvoidComponent>     { static std::vector<AvoidComponent>&     Vector(Allocators& a) { return a.avoid; } };
template<> struct GetAllocatorInternal<AvoidThisComponent> { static std::vector<AvoidThisComponent>& Vector(Allocators& a) { return a.avoidThis; } };

struct AvoidThis
{
	PositionComponent& pos;
	MoveComponent& move;
	SpriteComponent& sprite;
	AvoidThisComponent& avoid;
	
    AvoidThis(Allocators& a, const WorldBoundsComponent& bounds)
        // position it in small area near center of world bounds
		: pos(a.New<PositionComponent>(
		      RandomFloat(bounds.xMin, bounds.xMax) * 0.2f,
			  RandomFloat(bounds.yMin, bounds.yMax) * 0.2f))
		, move(a.New<MoveComponent>(0.1f, 0.2f, pos))
        // setup a sprite for it (6th one), and a random color
		, sprite(a.New<SpriteComponent>(
		         RandomFloat(0.5f, 1.0f),
		         RandomFloat(0.5f, 1.0f),
		         RandomFloat(0.5f, 1.0f),
		         5,
		         2.0f,
		         pos))
        // setup an "avoid this" component
		, avoid(a.New<AvoidThisComponent>(1.3f, sprite))
	{
	}
};

struct RegularObject
{
	PositionComponent& pos;
	MoveComponent& move;
	SpriteComponent& sprite;
	AvoidComponent& avoid;
	
    RegularObject(Allocators& a, const WorldBoundsComponent& bounds)
        // position it within world bounds
		: pos(a.New<PositionComponent>(
		      RandomFloat(bounds.xMin, bounds.xMax),
			  RandomFloat(bounds.yMin, bounds.yMax)))
		, move(a.New<MoveComponent>(0.5f, 0.7f, pos))
        // setup a sprite for it (random sprite index from first 5), and initial white color
		, sprite(a.New<SpriteComponent>(
		         1.0f,
		         1.0f,
		         1.0f,
		         rand() % 5,
		         1.0f,
		         pos))
		, avoid(a.New<AvoidComponent>(move, sprite))
	{
	}
};

// -------------------------------------------------------------------------------------------------
// "the game"

struct Game
{
	WorldBoundsComponent bounds;
	std::vector<AvoidThis> avoidThis;
	std::vector<RegularObject> regularObject;
	Allocators components;

	Game(const WorldBoundsComponent& bounds)
		: bounds(bounds)
	{
		components.SizeHint(kObjectCount + kAvoidCount);

		// create regular objects that move
		regularObject.reserve(kObjectCount);
		for (auto i = 0; i < kObjectCount; ++i)
			regularObject.emplace_back(components, bounds);

		// create objects that should be avoided
		avoidThis.reserve(kAvoidCount);
		for (auto i = 0; i < kAvoidCount; ++i)
			avoidThis.emplace_back(components, bounds);
	}
	void Clear()
	{
		avoidThis.clear();
		regularObject.clear();
	}
};


template<class T>
void ExportSpriteData(const T& go, sprite_data_t& spr)
{
    // write out their Position & Sprite data into destination buffer that will be rendered later on.
    //
    // Using a smaller global scale "zooms out" the rendering, so to speak.
    float globalScale = 0.05f;
    spr.posX = go.pos.x * globalScale;
    spr.posY = go.pos.y * globalScale;
    spr.scale = go.scale * globalScale;
    spr.colR = go.colorR;
    spr.colG = go.colorG;
    spr.colB = go.colorB;
    spr.sprite = (float)go.spriteIndex;
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
		c.UpdatePosition(deltaTime, s_game->bounds);
	}
	
    // Resolve all collisions
    for (auto& c : s_game->components.avoid)
	{
		c.ResolveCollisions(deltaTime, s_game->components.avoidThis);
    }

	int objectCount = 0;
    for (auto& c : s_game->components.sprite)
	{
		ExportSpriteData(c, data[objectCount++]);
    }
    return objectCount;
}

