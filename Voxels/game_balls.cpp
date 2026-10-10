#include "game_balls.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

game_balls::~game_balls()
{
    if (B2_IS_NON_NULL(m_world))
        b2DestroyWorld(m_world);
}

b2BodyId game_balls::box(float x, float z, float half_x, float half_z)
{
    auto body_def = b2DefaultBodyDef();
    body_def.position = {x, z};
    const auto body = b2CreateBody(m_world, &body_def);
    const auto polygon = b2MakeBox(half_x, half_z);
    auto shape = b2DefaultShapeDef();
    shape.material.friction = 0.3f;
    shape.material.restitution = 0.75f;
    b2CreatePolygonShape(body, &shape, &polygon);
    return body;
}

void game_balls::reset(const maze_game_model &board, std::mt19937 &rng)
{
    if (B2_IS_NON_NULL(m_world))
        b2DestroyWorld(m_world);
    auto world_def = b2DefaultWorldDef();
    world_def.gravity = {0, 0};
    world_def.hitEventThreshold = 0.1f;
    m_world = b2CreateWorld(&world_def);
    m_balls.clear();
    m_accumulator = 0;
    m_bricks.assign(board.cells().size(), b2_nullBodyId);
    const float side = static_cast<float>(board.side());
    const float center = (side - 1) * 0.5f;
    box(-1, center, 0.5f, side * 0.5f + 1);
    box(side, center, 0.5f, side * 0.5f + 1);
    box(center, -1, side * 0.5f + 1, 0.5f);
    box(center, side, side * 0.5f + 1, 0.5f);
    sync_bricks(board);
    std::vector<std::size_t> squares;
    for (std::size_t i = 0; i < board.cells().size(); ++i)
        if (board.cells()[i] != maze_game_model::cell_kind::bad &&
            board.cells()[i] != maze_game_model::cell_kind::goal &&
            board.cells()[i] != maze_game_model::cell_kind::start)
            squares.push_back(i);
    std::shuffle(squares.begin(), squares.end(), rng);
    const auto count = std::min({MAX_BALLS, std::max<std::size_t>(4, board.side()), squares.size()});
    constexpr std::array<std::array<float, 3>, 5> colors{{
        {0.85f, 0.35f, 0.12f}, {0.75f, 0.8f, 0.9f}, {0.15f, 0.6f, 0.8f},
        {0.7f, 0.18f, 0.4f}, {0.7f, 0.55f, 0.2f}}};
    for (std::size_t i = 0; i < count; ++i)
    {
        auto body_def = b2DefaultBodyDef();
        body_def.type = b2_dynamicBody;
        body_def.position = {static_cast<float>(squares[i] % board.side()),
                             static_cast<float>(squares[i] / board.side())};
        body_def.linearDamping = 0.08f;
        body_def.angularDamping = 0.10f;
        body_def.isBullet = true;
        const auto body = b2CreateBody(m_world, &body_def);
        auto shape = b2DefaultShapeDef();
        shape.density = 1;
        shape.material.friction = 0.3f;
        shape.material.restitution = 0.75f;
        shape.enableHitEvents = true;
        const b2Circle circle{{0, 0}, RADIUS};
        b2CreateCircleShape(body, &shape, &circle);
        const float angle = std::uniform_real_distribution<float>(0, 6.2831853f)(rng);
        b2Body_SetLinearVelocity(body, {std::cos(angle) * 0.4f, std::sin(angle) * 0.4f});
        m_balls.push_back({body, static_cast<material>(std::uniform_int_distribution<int>(0, 2)(rng)),
                          colors[std::uniform_int_distribution<std::size_t>(0, colors.size() - 1)(rng)]});
    }
}

void game_balls::sync_bricks(const maze_game_model &board)
{
    for (std::size_t i = 0; i < board.cells().size(); ++i)
    {
        if (board.cells()[i] != maze_game_model::cell_kind::bad || B2_IS_NON_NULL(m_bricks[i]))
            continue;
        const b2Vec2 brick{static_cast<float>(i % board.side()), static_cast<float>(i / board.side())};
        // Revealing a brick must not trap a ball inside a newly created static shape.
        for (const auto &ball : m_balls)
        {
            const auto p = b2Body_GetPosition(ball.body);
            if (std::abs(p.x - brick.x) >= 0.5f + RADIUS || std::abs(p.y - brick.y) >= 0.5f + RADIUS)
                continue;
            float best = INFINITY;
            b2Vec2 destination{};
            for (std::size_t cell = 0; cell < board.cells().size(); ++cell)
            {
                if (board.cells()[cell] == maze_game_model::cell_kind::bad)
                    continue;
                const b2Vec2 candidate{static_cast<float>(cell % board.side()),
                                       static_cast<float>(cell / board.side())};
                const bool occupied = std::any_of(m_balls.begin(), m_balls.end(), [&](const auto &other)
                {
                    if (B2_ID_EQUALS(other.body, ball.body))
                        return false;
                    const auto q = b2Body_GetPosition(other.body);
                    return std::hypot(q.x - candidate.x, q.y - candidate.y) < 2 * RADIUS;
                });
                const float distance = std::hypot(candidate.x - p.x, candidate.y - p.y);
                if (!occupied && distance < best)
                {
                    best = distance;
                    destination = candidate;
                }
            }
            if (!std::isfinite(best))
            {
                SDL_LogError(SDL_LOG_CATEGORY_ERROR, "No free square available for a displaced ball");
                throw std::runtime_error("No free square available for a displaced ball");
            }
            b2Body_SetTransform(ball.body, destination, b2Body_GetRotation(ball.body));
        }
        m_bricks[i] = box(brick.x, brick.y, 0.5f, 0.5f);
    }
}

std::size_t game_balls::pack(std::array<float, MAX_BALLS * 4> &spheres, std::array<float, MAX_BALLS * 4> &surfaces) const
{
    spheres.fill(0);
    surfaces.fill(0);
    for (std::size_t i = 0; i < m_balls.size(); ++i)
    {
        const auto position = b2Body_GetPosition(m_balls[i].body);
        spheres[i * 4] = position.x;
        spheres[i * 4 + 1] = HEIGHT;
        spheres[i * 4 + 2] = position.y;
        spheres[i * 4 + 3] = RADIUS;
        std::copy(m_balls[i].color.begin(), m_balls[i].color.end(), surfaces.begin() + i * 4);
        surfaces[i * 4 + 3] = static_cast<float>(m_balls[i].surface);
    }
    return m_balls.size();
}

bool game_balls::update(float seconds)
{
    if (!std::isfinite(seconds) || seconds < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Invalid ball physics delta time");
        throw std::invalid_argument("Invalid ball physics delta time");
    }
    m_accumulator += std::min(seconds, 0.1f);
    bool hit = false;
    while (m_accumulator >= FIXED_STEP)
    {
        b2World_Step(m_world, FIXED_STEP, 4);
        const auto events = b2World_GetContactEvents(m_world);
        if (events.hitCount > 0)
        {
            hit = true;
            m_last_hit = events.hitEvents[events.hitCount - 1].point;
        }
        m_accumulator -= FIXED_STEP;
    }
    return hit;
}

bool game_balls::swipe(b2Vec2 from, b2Vec2 to)
{
    const float dx = to.x - from.x, dz = to.y - from.y;
    const float length = std::hypot(dx, dz);
    if (length <= 0.0001f)
        return false;
    bool hit = false;
    for (const auto &ball : m_balls)
    {
        const auto p = b2Body_GetPosition(ball.body);
        const float t = std::clamp(((p.x - from.x) * dx + (p.y - from.y) * dz) / (length * length), 0.0f, 1.0f);
        const b2Vec2 contact{from.x + dx * t, from.y + dz * t};
        if (std::hypot(p.x - contact.x, p.y - contact.y) > RADIUS + 0.08f)
            continue;
        const auto velocity = b2Body_GetLinearVelocity(ball.body);
        const float target_speed = std::min(8.0f, length * 12.0f);
        const float along = (velocity.x * dx + velocity.y * dz) / length;
        const float change = std::max(0.0f, target_speed - along);
        b2Vec2 desired{velocity.x + dx / length * change, velocity.y + dz / length * change};
        const float speed = std::hypot(desired.x, desired.y);
        if (speed > 8.0f)
        {
            desired.x *= 8.0f / speed;
            desired.y *= 8.0f / speed;
        }
        const float mass = b2Body_GetMass(ball.body);
        b2Body_ApplyLinearImpulse(ball.body, {mass * (desired.x - velocity.x), mass * (desired.y - velocity.y)},
                                 contact, true);
        m_last_hit = p;
        hit = true;
    }
    return hit;
}
