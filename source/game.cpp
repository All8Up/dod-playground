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
	
    SpriteComponent(float colorR, float colorG, float colorB, int spriteIndex, float scale)
		: colorR(colorR)
		, colorG(colorG)
		, colorB(colorB)
		, spriteIndex(spriteIndex)
		, scale(scale)
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
    
    MoveComponent(float minSpeed, float maxSpeed)
    {
        // random angle
        float angle = RandomFloat01() * 3.1415926f * 2;
        // random movement speed between given min & max
        float speed = RandomFloat(minSpeed, maxSpeed);
        // velocity x & y components
        velx = cosf(angle) * speed;
        vely = sinf(angle) * speed;
    }

    void UpdatePosition(float deltaTime, PositionComponent& pos, WorldBoundsComponent& bounds)
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
	
    AvoidThisComponent(float distance)
		: distance(distance)
    {
	}
};

// Objects with this component "avoid" objects with AvoidThis component:
// - when they get closer to them than Avoid::distance, they bounce back,
// - also they take sprite color from the object they just bumped into
struct AvoidComponent
{
    static float DistanceSq(const PositionComponent& a, const PositionComponent& b)
    {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }
    
    void ResolveCollision(float deltaTime, MoveComponent& move, PositionComponent& pos)
    {
        // flip velocity
        move.velx = -move.velx;
        move.vely = -move.vely;
        
        // move us out of collision, by moving just a tiny bit more than we'd normally move during a frame
        pos.x += move.velx * deltaTime * 1.1f;
        pos.y += move.vely * deltaTime * 1.1f;
    }
	
	template<class T>
    void ResolveCollisions(float deltaTime, SpriteComponent& mySprite, MoveComponent& myMove, PositionComponent& myPosition, std::vector<T>& avoidList)
    {
        // check each thing in avoid list
        for (auto& o : avoidList)
        {
            // is our position closer to "thing to avoid" position than the avoid distance?
            if (DistanceSq(myPosition, o.pos) < o.avoid.distance * o.avoid.distance)
            {
                ResolveCollision(deltaTime, myMove, myPosition);

                // also make our sprite take the color of the thing we just bumped into
                mySprite.colorR = o.sprite.colorR;
                mySprite.colorG = o.sprite.colorG;
                mySprite.colorB = o.sprite.colorB;
            }
        }
    }
};

struct WorldBounds
{
	WorldBoundsComponent wb;

	WorldBounds(const WorldBoundsComponent& bounds)
		: wb(bounds)
	{}
};

struct AvoidThis
{
	AvoidThisComponent avoid;
	PositionComponent pos;
	MoveComponent move;
	SpriteComponent sprite;
	
    AvoidThis(const WorldBoundsComponent& bounds)
		: move(0.1f, 0.2f)
        // position it in small area near center of world bounds
		, pos(RandomFloat(bounds.xMin, bounds.xMax) * 0.2f,
			  RandomFloat(bounds.yMin, bounds.yMax) * 0.2f)
        // setup a sprite for it (6th one), and a random color
		, sprite(RandomFloat(0.5f, 1.0f),
		         RandomFloat(0.5f, 1.0f),
		         RandomFloat(0.5f, 1.0f),
		         5,
		         2.0f)
        // setup an "avoid this" component
		, avoid(1.3f)
	{
	}
};

struct RegularObject
{
	PositionComponent pos;
	SpriteComponent sprite;
	MoveComponent move;
	AvoidComponent avoid;
	
    RegularObject(const WorldBoundsComponent& bounds)
		: move(0.5f, 0.7f)
        // position it within world bounds
		, pos(RandomFloat(bounds.xMin, bounds.xMax),
			  RandomFloat(bounds.yMin, bounds.yMax))
        // setup a sprite for it (random sprite index from first 5), and initial white color
		, sprite(1.0f,
		         1.0f,
		         1.0f,
		         rand() % 5,
		         1.0f)
	{
	}
};

// -------------------------------------------------------------------------------------------------
// "the game"

struct Game
{
	WorldBounds bounds;
	std::vector<AvoidThis> avoidThis;
	std::vector<RegularObject> regularObject;

	Game(const WorldBoundsComponent& bounds)
		: bounds(bounds)
	{
		// create regular objects that move
		regularObject.reserve(kObjectCount);
		for (auto i = 0; i < kObjectCount; ++i)
			regularObject.emplace_back(bounds);

		// create objects that should be avoided
		avoidThis.reserve(kAvoidCount);
		for (auto i = 0; i < kAvoidCount; ++i)
			avoidThis.emplace_back(bounds);
	}
	void Clear()
	{
		avoidThis.clear();
		regularObject.clear();
	}
};


template<class Movable>
void UpdatePosition(float deltaTime, Movable& go, WorldBoundsComponent& bounds)
{
	go.move.UpdatePosition(deltaTime, go.pos, bounds);
}

template<class Avoider>
void ResolveCollisions(float deltaTime, Avoider& go, std::vector<AvoidThis>& avoidList)
{
	go.avoid.ResolveCollisions(deltaTime, go.sprite, go.move, go.pos, avoidList);
}

template<class T>
void ExportSpriteData(const T& go, sprite_data_t& spr)
{
    // write out their Position & Sprite data into destination buffer that will be rendered later on.
    //
    // Using a smaller global scale "zooms out" the rendering, so to speak.
    float globalScale = 0.05f;
    spr.posX = go.pos.x * globalScale;
    spr.posY = go.pos.y * globalScale;
    spr.scale = go.sprite.scale * globalScale;
    spr.colR = go.sprite.colorR;
    spr.colG = go.sprite.colorG;
    spr.colB = go.sprite.colorB;
    spr.sprite = (float)go.sprite.spriteIndex;
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
    for (auto& go : s_game->regularObject)
	{
		UpdatePosition(deltaTime, go, s_game->bounds.wb);
	}
    for (auto& go : s_game->avoidThis)
	{
		UpdatePosition(deltaTime, go, s_game->bounds.wb);
	}
	
    // Resolve all collisions
    for (auto& go : s_game->regularObject)
	{
		ResolveCollisions(deltaTime, go, s_game->avoidThis);
	}
	
    // Export rendering data
    int objectCount = 0;
    for (auto& go : s_game->regularObject)
	{
		ExportSpriteData(go, data[objectCount++]);
    }
    for (auto& go : s_game->avoidThis)
	{
		ExportSpriteData(go, data[objectCount++]);
    }
    return objectCount;
}

