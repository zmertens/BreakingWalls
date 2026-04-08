-- maze_gen.lua
-- Procedural maze generation using Recursive Backtracking (DFS).
-- Produces a grid of cells, each with four wall flags: N, S, E, W.
-- Suitable for any size grid; used for both the current chunk and
-- look-ahead chunk generation.

local M = {}

-- Direction constants
M.NORTH = 1
M.SOUTH = 2
M.EAST  = 3
M.WEST  = 4

-- Opposite direction lookup
local OPPOSITE = {
    [M.NORTH] = M.SOUTH,
    [M.SOUTH] = M.NORTH,
    [M.EAST]  = M.WEST,
    [M.WEST]  = M.EAST,
}

-- Grid deltas: row (y) and col (x) offsets per direction
local DR = { [M.NORTH] = -1, [M.SOUTH] = 1, [M.EAST] = 0, [M.WEST] = 0 }
local DC = { [M.NORTH] = 0,  [M.SOUTH] = 0, [M.EAST] = 1, [M.WEST] = -1 }

-- ─────────────────────────────────────────────────────────────────────────────
-- Public API
-- ─────────────────────────────────────────────────────────────────────────────

--- Create a blank grid (all walls intact, no passages).
-- @param rows  number of rows
-- @param cols  number of columns
-- @return 2-D array of cell tables
function M.create(rows, cols)
    local grid = {}
    for r = 1, rows do
        grid[r] = {}
        for c = 1, cols do
            grid[r][c] = {
                walls   = { [M.NORTH]=true, [M.SOUTH]=true, [M.EAST]=true, [M.WEST]=true },
                visited = false,
                exit    = false,
                pickup  = false,
            }
        end
    end
    return grid
end

--- Carve a perfect maze into *grid* using iterative DFS backtracking.
-- @param grid    the blank grid returned by M.create()
-- @param rows    number of rows
-- @param cols    number of columns
-- @param sr, sc  start row/col (1-indexed)
-- @param seed    random seed (optional; uses os.time() if omitted)
function M.generate(grid, rows, cols, sr, sc, seed)
    math.randomseed(seed or os.time())

    local stack = { { r = sr, c = sc } }
    grid[sr][sc].visited = true

    while #stack > 0 do
        local curr = stack[#stack]
        local r, c = curr.r, curr.c

        -- Collect unvisited neighbours in a random order
        local dirs = { M.NORTH, M.SOUTH, M.EAST, M.WEST }
        -- Fisher–Yates shuffle
        for i = #dirs, 2, -1 do
            local j = math.random(i)
            dirs[i], dirs[j] = dirs[j], dirs[i]
        end

        local moved = false
        for _, dir in ipairs(dirs) do
            local nr = r + DR[dir]
            local nc = c + DC[dir]
            if nr >= 1 and nr <= rows and nc >= 1 and nc <= cols and not grid[nr][nc].visited then
                -- Carve passage both ways
                grid[r][c].walls[dir]             = false
                grid[nr][nc].walls[OPPOSITE[dir]] = false
                grid[nr][nc].visited = true
                table.insert(stack, { r = nr, c = nc })
                moved = true
                break
            end
        end

        if not moved then
            table.remove(stack)
        end
    end
end

--- Mark the exit cell using BFS from (sr, sc) — the furthest reachable cell.
-- Also randomly scatters pickup flags through the maze.
-- @return exit row, exit col
function M.decorate(grid, rows, cols, sr, sc, pickup_chance)
    pickup_chance = pickup_chance or 0.12

    -- BFS to find furthest cell
    local dist = {}
    for r = 1, rows do
        dist[r] = {}
        for c = 1, cols do dist[r][c] = -1 end
    end
    dist[sr][sc] = 0
    local queue   = { { r = sr, c = sc, d = 0 } }
    local head    = 1
    local best    = queue[1]

    while head <= #queue do
        local curr = queue[head]; head = head + 1
        if curr.d > best.d then best = curr end

        local cell = grid[curr.r][curr.c]
        for _, dir in ipairs({ M.NORTH, M.SOUTH, M.EAST, M.WEST }) do
            if not cell.walls[dir] then
                local nr = curr.r + DR[dir]
                local nc = curr.c + DC[dir]
                if nr >= 1 and nr <= rows and nc >= 1 and nc <= cols and dist[nr][nc] == -1 then
                    dist[nr][nc] = curr.d + 1
                    table.insert(queue, { r = nr, c = nc, d = curr.d + 1 })
                end
            end
        end
    end

    grid[best.r][best.c].exit = true

    -- Scatter pickups (skip start and exit cells)
    for r = 1, rows do
        for c = 1, cols do
            if not grid[r][c].exit and not (r == sr and c == sc) then
                if math.random() < pickup_chance then
                    grid[r][c].pickup = true
                end
            end
        end
    end

    return best.r, best.c
end

--- Check whether a move from (r,c) in *dir* is valid (no wall).
function M.can_move(grid, rows, cols, r, c, dir)
    if r < 1 or r > rows or c < 1 or c > cols then return false end
    local cell = grid[r][c]
    if cell.walls[dir] then return false end
    local nr = r + DR[dir]
    local nc = c + DC[dir]
    return nr >= 1 and nr <= rows and nc >= 1 and nc <= cols
end

--- Return the neighbour cell coords after a move in *dir* from (r,c).
function M.neighbour(r, c, dir)
    return r + DR[dir], c + DC[dir]
end

-- Expose deltas for external coordinate conversion
M.DR = DR
M.DC = DC

return M
