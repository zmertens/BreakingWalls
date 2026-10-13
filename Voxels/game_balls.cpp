#include "game_balls.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

game_balls::~game_balls()
{
    if (B3_IS_NON_NULL(m_world))
        b3DestroyWorld(m_world);
}

b3BodyId game_balls::box(const b3Vec3 position, const b3Vec3 half_extents)
{
    auto body_def = b3DefaultBodyDef();
    body_def.position = position;
    const auto body = b3CreateBody(m_world, &body_def);
    const auto hull = b3MakeBoxHull(half_extents.x, half_extents.y, half_extents.z);
    auto shape = b3DefaultShapeDef();
    shape.baseMaterial.friction = 0.3f;
    shape.baseMaterial.restitution = 0.75f;
    b3CreateHullShape(body, &shape, &hull.base);
    return body;
}

void game_balls::reset(const maze_game_model &board, std::mt19937 &rng)
{
    if (B3_IS_NON_NULL(m_world))
        b3DestroyWorld(m_world);
    auto world_def = b3DefaultWorldDef();
    world_def.gravity = {0.0f, -9.8f, 0.0f};
    world_def.restitutionThreshold = 0.1f;
    world_def.hitEventThreshold = 0.1f;
    m_world = b3CreateWorld(&world_def);
    m_balls.clear();
    m_accumulator = 0;
    m_bricks.assign(board.cells().size(), b3_nullBodyId);
    const float side = static_cast<float>(board.side());
    const float center = (side - 1) * 0.5f;
    const float half_side = side * 0.5f + 1.0f;
    constexpr float room_height = 4.0f;
    const float wall_y = BOARD_FLOOR + room_height * 0.5f;
    box({center, BOARD_FLOOR - 0.5f, center}, {half_side, 0.5f, half_side});
    box({-1.0f, wall_y, center}, {0.5f, room_height * 0.5f, half_side});
    box({side, wall_y, center}, {0.5f, room_height * 0.5f, half_side});
    box({center, wall_y, -1.0f}, {half_side, room_height * 0.5f, 0.5f});
    box({center, wall_y, side}, {half_side, room_height * 0.5f, 0.5f});
    box({center, BOARD_FLOOR + room_height + 0.5f, center}, {half_side, 0.5f, half_side});
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
        auto body_def = b3DefaultBodyDef();
        body_def.type = b3_dynamicBody;
        body_def.position = {static_cast<float>(squares[i] % board.side()),
                             BOARD_FLOOR + RADIUS,
                             static_cast<float>(squares[i] / board.side())};
        body_def.linearDamping = 0.08f;
        body_def.angularDamping = 0.10f;
        body_def.isBullet = true;
        const auto body = b3CreateBody(m_world, &body_def);
        auto shape = b3DefaultShapeDef();
        shape.density = 1;
        shape.baseMaterial.friction = 0.3f;
        shape.baseMaterial.restitution = 0.75f;
        shape.enableHitEvents = true;
        const b3Sphere sphere{{0.0f, 0.0f, 0.0f}, RADIUS};
        b3CreateSphereShape(body, &shape, &sphere);
        const float angle = std::uniform_real_distribution<float>(0, 6.2831853f)(rng);
        b3Body_SetLinearVelocity(body, {std::cos(angle) * 0.4f, 0.0f, std::sin(angle) * 0.4f});
        m_balls.push_back({body, static_cast<material>(std::uniform_int_distribution<int>(0, 2)(rng)),
                          colors[std::uniform_int_distribution<std::size_t>(0, colors.size() - 1)(rng)]});
    }
}

void game_balls::sync_bricks(const maze_game_model &board)
{
    for (std::size_t i = 0; i < board.cells().size(); ++i)
    {
        if (board.cells()[i] != maze_game_model::cell_kind::bad || B3_IS_NON_NULL(m_bricks[i]))
            continue;
        const b3Vec3 brick{static_cast<float>(i % board.side()), 7.0f,
                           static_cast<float>(i / board.side())};
        for (const auto &ball : m_balls)
        {
            const auto p = b3Body_GetPosition(ball.body);
            const float dx = std::max(std::abs(p.x - brick.x) - 0.5f, 0.0f);
            const float dy = std::max(std::abs(p.y - brick.y) - 0.5f, 0.0f);
            const float dz = std::max(std::abs(p.z - brick.z) - 0.5f, 0.0f);
            if (dx * dx + dy * dy + dz * dz >= RADIUS * RADIUS)
                continue;
            float best = INFINITY;
            b3Vec3 destination{};
            for (std::size_t cell = 0; cell < board.cells().size(); ++cell)
            {
                if (board.cells()[cell] == maze_game_model::cell_kind::bad)
                    continue;
                const b3Vec3 candidate{static_cast<float>(cell % board.side()),
                                       p.y, static_cast<float>(cell / board.side())};
                const bool occupied = std::any_of(m_balls.begin(), m_balls.end(), [&](const auto &other)
                {
                    if (B3_ID_EQUALS(other.body, ball.body))
                        return false;
                    const auto q = b3Body_GetPosition(other.body);
                    return std::hypot(std::hypot(q.x - candidate.x, q.y - candidate.y),
                                      q.z - candidate.z) < 2 * RADIUS;
                });
                const float distance = std::hypot(candidate.x - p.x, candidate.z - p.z);
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
            b3Body_SetTransform(ball.body, destination, b3Body_GetRotation(ball.body));
        }
        m_bricks[i] = box(brick, {0.5f, 0.5f, 0.5f});
    }
}

std::size_t game_balls::pack(std::array<float, MAX_BALLS * 4> &spheres, std::array<float, MAX_BALLS * 4> &surfaces) const
{
    spheres.fill(0);
    surfaces.fill(0);
    for (std::size_t i = 0; i < m_balls.size(); ++i)
    {
        const auto position = b3Body_GetPosition(m_balls[i].body);
        spheres[i * 4] = position.x;
        spheres[i * 4 + 1] = position.y;
        spheres[i * 4 + 2] = position.z;
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
        b3World_Step(m_world, FIXED_STEP, 4);
        const auto events = b3World_GetContactEvents(m_world);
        if (events.hitCount > 0)
        {
            hit = true;
            m_last_hit = events.hitEvents[events.hitCount - 1].point;
        }
        m_accumulator -= FIXED_STEP;
    }
    return hit;
}

bool game_balls::swipe(const b3Vec3 from, const b3Vec3 to)
{
    const float dx = to.x - from.x, dz = to.z - from.z;
    const float length = std::hypot(dx, dz);
    if (length <= 0.0001f)
        return false;
    bool hit = false;
    for (const auto &ball : m_balls)
    {
        const auto p = b3Body_GetPosition(ball.body);
        const float t = std::clamp(((p.x - from.x) * dx + (p.z - from.z) * dz) /
                                   (length * length), 0.0f, 1.0f);
        const b3Vec3 contact{from.x + dx * t, p.y, from.z + dz * t};
        if (std::hypot(std::hypot(p.x - contact.x, p.y - contact.y), p.z - contact.z) > RADIUS + 0.08f)
            continue;
        const auto velocity = b3Body_GetLinearVelocity(ball.body);
        const float target_speed = std::min(8.0f, length * 12.0f);
        const float along = (velocity.x * dx + velocity.z * dz) / length;
        const float change = std::max(0.0f, target_speed - along);
        b3Vec3 desired{velocity.x + dx / length * change, velocity.y,
                       velocity.z + dz / length * change};
        const float speed = std::hypot(desired.x, desired.z);
        if (speed > 8.0f)
        {
            desired.x *= 8.0f / speed;
            desired.z *= 8.0f / speed;
        }
        const float mass = b3Body_GetMass(ball.body);
        b3Body_ApplyLinearImpulse(ball.body,
                                  {mass * (desired.x - velocity.x), 0.0f,
                                   mass * (desired.z - velocity.z)},
                                  contact, true);
        m_last_hit = p;
        hit = true;
    }
    return hit;
}
