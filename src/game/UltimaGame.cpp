#include "UltimaGame.h"
#include <cstdlib> // For rand()
#include <ctime>   // For time() for seeding rand()
#include <string>  // For std::string

// Definition for the static const float member
const float UltimaGame::INITIAL_FLOOR_CHANCE = 0.45f;
// MONSTER_BASE_HIT_CHANCE is constexpr in header
// MONSTER_HIT_CHANCE_PER_PLAYER_MOVE_INCREMENT is constexpr in header
// PLAYER_BASE_HIT_CHANCE is constexpr in header
// PLAYER_HIT_CHANCE_PER_LEVEL_INCREMENT is constexpr in header
// DUNGEON_TREASURE_CHANCE is constexpr in header, no definition needed here.

UltimaGame::UltimaGame() : player_x(MAP_WIDTH / 2), player_y(MAP_HEIGHT / 2), current_level(GameLevel::OVERWORLD), player_defeated_flag(false), current_cave_ptr(nullptr) {
    std::srand(std::time(0)); // Seed random number generator
    initializeOverworldMap();
    initializeStats(); // Initialize player stats
    turn_message = ""; // Initialize turn message
}

void UltimaGame::initializeStats() {
    hp = 15;
    max_hp = 15;
    level = 1;
    xp = 0;
    player_attack = PLAYER_ATTACK_DAMAGE; 
    player_defeated_flag = false;
    player_moves_count = 0; // Initialize player moves count
}

void UltimaGame::initializeOverworldMap() {
    game_map.clear();
    cave_states.clear(); // Clear existing cave states before generating new ones
    for (int i = 0; i < MAP_HEIGHT; ++i) {
        String row = "";
        for (int j = 0; j < MAP_WIDTH; ++j) {
            if (i == 0 || i == MAP_HEIGHT - 1 || j == 0 || j == MAP_WIDTH - 1) {
                row += T_OVERWORLD_WALL; // Use defined constant for boundary
            } else {
                int r = rand() % 100; // Percentage roll
                if (r < 6) { // 6% chance for an Open Cave
                    row += T_OPEN_CAVE; // Use new open cave tile
                    cave_states.emplace_back(j, i); // Add new cave state, j is x, i is y
                } else if (r < 9) { // 3% chance for an Oasis (6 to 8)
                    row += T_OASIS;
                } else { // Remaining 91% chance for Sand
                    row += T_SAND;
                }
            }
        }
        game_map.push_back(row);
    }
    // Ensure player starting position is sand
    if (player_x > 0 && player_x < MAP_WIDTH -1 && player_y > 0 && player_y < MAP_HEIGHT -1) {
        game_map[player_y][player_x] = T_SAND;
    }
}

// New helper: Counts "alive" (floor) neighbors for a cell
int UltimaGame::countAliveNeighbors(const std::vector<String>& map_to_check, int x, int y, char wall_tile, char floor_tile) {
    int count = 0;
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            if (i == 0 && j == 0) continue; // Skip the cell itself

            int neighbor_x = x + j;
            int neighbor_y = y + i;

            if (neighbor_x >= 0 && neighbor_x < MAP_WIDTH && neighbor_y >= 0 && neighbor_y < MAP_HEIGHT) {
                if (map_to_check[neighbor_y][neighbor_x] == floor_tile) {
                    count++;
                }
            } else {
                // Consider out-of-bounds as walls for border cells
                count++; // Effectively, this counts towards the wall condition if T_DUNGEON_WALL is the "alive" state for this interpretation
            }
        }
    }
    return count;
}

// New helper: Runs one iteration of cellular automata
void UltimaGame::runCellularAutomataIteration(std::vector<String>& map_to_smooth) {
    std::vector<String> next_map_state = map_to_smooth; // Create a copy to base changes on

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            // Keep outer border as walls
            if (y == 0 || y == MAP_HEIGHT - 1 || x == 0 || x == MAP_WIDTH - 1) {
                next_map_state[y][x] = T_DUNGEON_WALL;
                continue;
            }

            int alive_neighbors = countAliveNeighbors(map_to_smooth, x, y, T_DUNGEON_WALL, T_DUNGEON_FLOOR);

            if (map_to_smooth[y][x] == T_DUNGEON_FLOOR) {
                // A floor tile becomes a wall if it has less than (9 - WALL_THRESHOLD) floor neighbours
                // (i.e., WALL_THRESHOLD or more wall neighbours, assuming 8 neighbours total)
                // For typical CA, if floor neighbours < 4 (i.e. >4 wall neighbours), it dies (becomes wall)
                // If WALL_THRESHOLD = 5, means if floor_neighbours < 4, it becomes wall.
                if (alive_neighbors < (8 - WALL_THRESHOLD +1) ) { // +1 because count is of floor, threshold is for walls
                     next_map_state[y][x] = T_DUNGEON_WALL;
                }
            } else { // It's a wall tile
                // A wall tile becomes a floor if it has more than WALL_THRESHOLD floor neighbours
                // For typical CA, if wall neighbours > 4 (i.e. >4 floor neighbours), it becomes floor.
                // If WALL_THRESHOLD = 5, means if floor_neighbours > 5, it becomes floor
                if (alive_neighbors > WALL_THRESHOLD) {
                    next_map_state[y][x] = T_DUNGEON_FLOOR;
                }
            }
        }
    }
    map_to_smooth = next_map_state; // Apply the changes
}

// New helper: Flood fill to find connected areas
void UltimaGame::floodFill(int x, int y, const std::vector<String>& map_to_fill, std::vector<Point>& current_area_tiles, std::vector<std::vector<bool>>& visited, char floor_tile) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT || visited[y][x] || map_to_fill[y][x] != floor_tile) {
        return;
    }

    visited[y][x] = true;
    current_area_tiles.push_back(Point(x, y));

    floodFill(x + 1, y, map_to_fill, current_area_tiles, visited, floor_tile);
    floodFill(x - 1, y, map_to_fill, current_area_tiles, visited, floor_tile);
    floodFill(x, y + 1, map_to_fill, current_area_tiles, visited, floor_tile);
    floodFill(x, y - 1, map_to_fill, current_area_tiles, visited, floor_tile);
}

// New helper: Finds the largest connected area of floor_tile
void UltimaGame::findLargestConnectedArea(std::vector<Point>& out_largest_area_tiles, std::vector<String>& map_to_analyze, char floor_tile) {
    out_largest_area_tiles.clear();
    std::vector<std::vector<bool>> visited(MAP_HEIGHT, std::vector<bool>(MAP_WIDTH, false));
    
    for (int y = 0; y < MAP_HEIGHT; ++y) {
        for (int x = 0; x < MAP_WIDTH; ++x) {
            if (map_to_analyze[y][x] == floor_tile && !visited[y][x]) {
                std::vector<Point> current_area;
                floodFill(x, y, map_to_analyze, current_area, visited, floor_tile);
                if (current_area.size() > out_largest_area_tiles.size()) {
                    out_largest_area_tiles = current_area;
                }
            }
        }
    }
}

void UltimaGame::initializeDungeonMap(int from_cave_x, int from_cave_y) {
    dungeon_map.clear();
    monsters.clear(); // Clear monsters for new dungeon
    player_defeated_flag = false; // Reset defeat flag
    current_cave_ptr = nullptr; // Reset first
    for (auto& cave : cave_states) {
        if (cave.overworld_x == from_cave_x && cave.overworld_y == from_cave_y) {
            current_cave_ptr = &cave;
            break;
        }
    }
    // If current_cave_ptr is still nullptr here, it means we entered a cave not in cave_states (should not happen with current logic)
    // Or, if this is the first entry, monsters_remaining_in_dungeon will be 0 from constructor, update it after spawning.

    std::vector<Point> largest_floor_area;
    bool suitable_dungeon_generated = false;

    for (int attempt = 0; attempt < MAX_DUNGEON_GENERATION_ATTEMPTS; ++attempt) {
        // 1. Initial Randomization
        std::vector<String> temp_dungeon_map;
        for (int i = 0; i < MAP_HEIGHT; ++i) {
            String row = "";
            for (int j = 0; j < MAP_WIDTH; ++j) {
                if (i == 0 || i == MAP_HEIGHT - 1 || j == 0 || j == MAP_WIDTH - 1) {
                    row += T_DUNGEON_WALL; // Outer border is always wall
                } else {
                    if ((float)rand() / RAND_MAX < INITIAL_FLOOR_CHANCE) {
                        row += T_DUNGEON_FLOOR;
                    } else {
                        row += T_DUNGEON_WALL;
                    }
                }
            }
            temp_dungeon_map.push_back(row);
        }

        // 2. Cellular Automata Iterations
        for (int i = 0; i < DUNGEON_GENERATION_ITERATIONS; ++i) {
            runCellularAutomataIteration(temp_dungeon_map);
        }

        // 3. Connectivity Analysis
        largest_floor_area.clear();
        findLargestConnectedArea(largest_floor_area, temp_dungeon_map, T_DUNGEON_FLOOR);

        if (largest_floor_area.size() >= MIN_DUNGEON_FLOOR_TILES) {
            dungeon_map = temp_dungeon_map; // Keep this generated map

            // Mark only the largest area as floor, rest as wall
            for(int y=0; y < MAP_HEIGHT; ++y) {
                for(int x=0; x < MAP_WIDTH; ++x) {
                    if (dungeon_map[y][x] == T_DUNGEON_FLOOR) {
                         bool in_largest_area = false;
                         for(const auto& p : largest_floor_area) {
                             if(p.x == x && p.y == y) {
                                 in_largest_area = true;
                                 break;
                             }
                         }
                         if (!in_largest_area) {
                             dungeon_map[y][x] = T_DUNGEON_WALL; // Fill disconnected areas
                         }
                    }
                }
            }
            // Re-run findLargestConnectedArea on the cleaned map to ensure largest_floor_area points are valid.
            findLargestConnectedArea(largest_floor_area, dungeon_map, T_DUNGEON_FLOOR);

            suitable_dungeon_generated = true;
            break; // Exit retry loop
        }
    } // End of generation attempts loop

    if (!suitable_dungeon_generated || largest_floor_area.empty()) {
        // Fallback: create a 5x5 open room if generation fails or area is too small / empty
        dungeon_map.assign(MAP_HEIGHT, String(std::string(MAP_WIDTH, T_DUNGEON_WALL).c_str())); // Fill with walls
        int center_x = MAP_WIDTH / 2;
        int center_y = MAP_HEIGHT / 2;
        int room_size = 5; // For 5x5 room
        int half_room = room_size / 2;

        largest_floor_area.clear();
        for(int r = -half_room; r <= half_room; ++r) {
            for(int c = -half_room; c <= half_room; ++c) {
                int current_y = center_y + r;
                int current_x = center_x + c;
                if(current_y > 0 && current_y < MAP_HEIGHT -1 && current_x > 0 && current_x < MAP_WIDTH -1) { // Ensure within inner map bounds
                    dungeon_map[current_y][current_x] = T_DUNGEON_FLOOR;
                    largest_floor_area.push_back(Point(current_x, current_y));
                }
            }
        }
        // If largest_floor_area is still empty (e.g. MAP_WIDTH/HEIGHT too small for 5x5), use at least one tile.
        if (largest_floor_area.empty()) {
            if (center_y > 0 && center_y < MAP_HEIGHT -1 && center_x > 0 && center_x < MAP_WIDTH -1) {
                dungeon_map[center_y][center_x] = T_DUNGEON_FLOOR;
                largest_floor_area.push_back(Point(center_x, center_y));
            } else { // Absolute fallback: top-left accessible
                 dungeon_map[1][1] = T_DUNGEON_FLOOR;
                 largest_floor_area.push_back(Point(1,1));
            }
        }
    }

    // 4. Player and Stair Placement (common for generated or fallback)
    Point stairs_up_pos(-1,-1);
    std::vector<Point> available_for_spawn; // Ensure this is declared before the if/else

    if (largest_floor_area.empty()) {
        // Absolute last resort: place player at fixed point if everything failed.
        player_x = MAP_WIDTH / 2;
        player_y = MAP_HEIGHT / 2;
        if (player_y >=0 && player_y < MAP_HEIGHT && player_x >=0 && player_x < MAP_WIDTH) {
           dungeon_map[player_y][player_x] = T_DUNGEON_FLOOR; // Ensure it's floor
        } else { // if map is tiny
           player_x = 1; player_y = 1;
           dungeon_map[1][1] = T_DUNGEON_FLOOR;
        }
        // Attempt to place stairs up somewhere, anywhere if possible
        if(player_y - 1 > 0) dungeon_map[player_y - 1][player_x] = T_STAIRS_UP;
        else if (player_x + 1 < MAP_WIDTH -1) dungeon_map[player_y][player_x+1] = T_STAIRS_UP;
        // available_for_spawn remains empty in this case
    } else {
        // Place Player
        int player_start_index = rand() % largest_floor_area.size();
        Point player_start_pos = largest_floor_area[player_start_index];
        player_x = player_start_pos.x;
        player_y = player_start_pos.y;
        dungeon_map[player_y][player_x] = T_DUNGEON_FLOOR; 

        available_for_spawn = largest_floor_area; // Assign to the already declared vector
        available_for_spawn.erase(std::remove_if(available_for_spawn.begin(), available_for_spawn.end(),
                                           [&](const Point& p){ return p.x == player_x && p.y == player_y; }),
                           available_for_spawn.end());

        if (available_for_spawn.empty()) {
            // Only one spot, player took it. Try adjacent for stairs.
            bool placed_stairs = false;
            int offsets[] = {-1, 1};
            for(int dx_offset : offsets) { // Check horizontal first
                if (player_x + dx_offset > 0 && player_x + dx_offset < MAP_WIDTH -1 && dungeon_map[player_y][player_x + dx_offset] == T_DUNGEON_FLOOR) {
                    dungeon_map[player_y][player_x + dx_offset] = T_STAIRS_UP;
                    stairs_up_pos = Point(player_x + dx_offset, player_y); // Track where stairs were placed
                    placed_stairs = true; break;
                }
            }
            if(!placed_stairs) {
                for(int dy_offset : offsets) { // Check vertical
                     if (player_y + dy_offset > 0 && player_y + dy_offset < MAP_HEIGHT -1 && dungeon_map[player_y + dy_offset][player_x] == T_DUNGEON_FLOOR) {
                        dungeon_map[player_y + dy_offset][player_x] = T_STAIRS_UP;
                        stairs_up_pos = Point(player_x, player_y + dy_offset); // Track where stairs were placed
                        placed_stairs = true; break;
                    }
                }
            }
        } else {
            int stairs_up_index = rand() % available_for_spawn.size();
            stairs_up_pos = available_for_spawn[stairs_up_index];
            dungeon_map[stairs_up_pos.y][stairs_up_pos.x] = T_STAIRS_UP;
            
            // Remove stairs pos from spawn consideration
            available_for_spawn.erase(std::remove_if(available_for_spawn.begin(), available_for_spawn.end(),
                                           [&](const Point& p){ return p.x == stairs_up_pos.x && p.y == stairs_up_pos.y; }),
                               available_for_spawn.end());
        }
    }

    // 5. Treasure Placement
    if (((float)rand() / RAND_MAX) < CAVE_HAS_TREASURE_CHANCE) {
        if (!available_for_spawn.empty()) {
            int treasure_spawn_idx = rand() % available_for_spawn.size();
            Point treasure_pos = available_for_spawn[treasure_spawn_idx];
            dungeon_map[treasure_pos.y][treasure_pos.x] = T_TREASURE_MAP_CHAR;
            // Remove treasure spot from further monster spawn consideration
            available_for_spawn.erase(available_for_spawn.begin() + treasure_spawn_idx);
        }
    }

    // 6. Monster Spawning
    // std::vector<Point>& spawn_points = available_for_spawn; // Alias for clarity, or just use available_for_spawn
    int max_spawnable_here = MAX_MONSTERS_PER_DUNGEON;
    if (available_for_spawn.size() < max_spawnable_here) { // Use available_for_spawn directly
        max_spawnable_here = available_for_spawn.size();
    }
    int num_monsters_to_spawn = 0;
    if (max_spawnable_here > 0) {
        num_monsters_to_spawn = rand() % (max_spawnable_here + 1); 
    }
    for (int i = 0; i < num_monsters_to_spawn; ++i) {
        if (available_for_spawn.empty()) break; 
        int monster_spawn_idx = rand() % available_for_spawn.size();
        Point monster_pos = available_for_spawn[monster_spawn_idx];
        int monster_hp = (rand() % 9) + 1; 
        monsters.emplace_back(monster_pos.x, monster_pos.y, monster_hp, MONSTER_ATTACK_DAMAGE);
        available_for_spawn.erase(available_for_spawn.begin() + monster_spawn_idx); 
    }

    if (current_cave_ptr) { // After monsters are spawned
        current_cave_ptr->monsters_remaining_in_dungeon = monsters.size();
        // turns_until_monsters_emerge should remain -1 unless player leaves with monsters active
    }
}

void UltimaGame::movePlayer(int dx, int dy) {
    if (player_defeated_flag) return; 
    clearTurnMessage(); 

    int new_x = player_x + dx;
    int new_y = player_y + dy;

    if (current_level == GameLevel::OVERWORLD) {
        // Check for overworld monster at target location first
        Monster* target_overworld_monster = nullptr;
        for (auto& monster : overworld_monsters) {
            if (monster.active && monster.x == new_x && monster.y == new_y) {
                target_overworld_monster = &monster;
                break;
            }
        }
        if (target_overworld_monster) {
            turn_message += resolveCombat(*target_overworld_monster); // Append combat message
            // Player does not move into monster's square for now
        } else {
            // Original overworld movement logic
            if (new_x >= 0 && new_x < MAP_WIDTH && new_y >= 0 && new_y < MAP_HEIGHT) {
                char target_tile = game_map[new_y][new_x];
                if (target_tile != T_OVERWORLD_WALL && target_tile != T_SEALED_CAVE) { // Cannot walk on sealed caves
                    player_x = new_x;
                    player_y = new_y;
                    player_moves_count++;
                } else if (target_tile == T_SEALED_CAVE) {
                    turn_message += "The cave is sealed. ";
                }
            }
        }
    } else { // GameLevel::DUNGEON
        if (new_x >= 0 && new_x < MAP_WIDTH && new_y >= 0 && new_y < MAP_HEIGHT) {
            // Check for monster at target location first
            Monster* target_monster = nullptr;
            for (auto& monster : monsters) {
                if (monster.active && monster.x == new_x && monster.y == new_y) {
                    target_monster = &monster;
                    break;
                }
            }

            if (target_monster) {
                turn_message += resolveCombat(*target_monster); // Append combat message
                // Player does not move into monster's square unless monster is defeated AND we then allow it.
                // For now, combat resolution itself is the action.
                // If monster was defeated, it's now inactive. Player could move in a subsequent turn or if we add post-combat move here.
            } else {
                char target_tile = dungeon_map[new_y][new_x];
                // Allow moving onto floor, stairs, or treasure tiles
                if (target_tile == T_DUNGEON_FLOOR || target_tile == T_STAIRS_UP || target_tile == T_TREASURE_MAP_CHAR) { 
                    player_x = new_x;
                    player_y = new_y;
                    player_moves_count++;
                    // turn_message += "Moved. "; // Optional: uncomment for move confirmation
                } else if (target_tile == T_DUNGEON_WALL) {
                    turn_message += "Blocked by a wall. ";
                } else {
                    // Potentially other non-passable dungeon features in future
                    turn_message += "Cannot move there. ";
                }
            }
        }
    }
}

String UltimaGame::renderView() {
    String view_str = "";
    int view_start_x = player_x - VIEW_WIDTH / 2;
    int view_start_y = player_y - VIEW_HEIGHT / 2;

    const std::vector<String>* current_map_ptr = (current_level == GameLevel::OVERWORLD) ? &game_map : &dungeon_map;

    for (int y_offset = 0; y_offset < VIEW_HEIGHT; ++y_offset) {
        for (int x_offset = 0; x_offset < VIEW_WIDTH; ++x_offset) {
            int map_render_x = view_start_x + x_offset;
            int map_render_y = view_start_y + y_offset;

            if (map_render_x == player_x && map_render_y == player_y) {
                view_str += T_PLAYER;
            } else {
                bool monster_rendered = false;
                // Render dungeon monsters if in dungeon
                if (current_level == GameLevel::DUNGEON) {
                    for (const auto& monster : monsters) {
                        if (monster.active && monster.x == map_render_x && monster.y == map_render_y) {
                            view_str += T_MONSTER;
                            monster_rendered = true;
                            break;
                        }
                    }
                } 
                // Render overworld monsters if on overworld
                else if (current_level == GameLevel::OVERWORLD) {
                    for (const auto& monster : overworld_monsters) {
                        if (monster.active && monster.x == map_render_x && monster.y == map_render_y) {
                            view_str += T_MONSTER;
                            monster_rendered = true;
                            break;
                        }
                    }
                }

                if (!monster_rendered) {
                    if (map_render_x >= 0 && map_render_x < MAP_WIDTH &&
                        map_render_y >= 0 && map_render_y < MAP_HEIGHT) {
                        char tile = (*current_map_ptr)[map_render_y][map_render_x];
                        if (current_level == GameLevel::DUNGEON && tile == T_TREASURE_MAP_CHAR) {
                            view_str += T_TREASURE_SYMBOL;
                        } else {
                            view_str += tile;
                        }
                    } else {
                        view_str += ' '; 
                    }
                }
            }
        }
        if (y_offset < VIEW_HEIGHT - 1) {
             view_str += "\n";
        }
    }
    return view_str;
}

String UltimaGame::searchCurrentTile() {
    // clearTurnMessage(); // Player action messages are now cleared at start of movePlayer or searchCurrentTile
    if (current_level == GameLevel::OVERWORLD) {
        char tile_char = game_map[player_y][player_x];
        clearTurnMessage(); // Clear message at start of action
        switch (tile_char) {
            case T_OPEN_CAVE:
                // Find the cave state
                for (auto& cave : cave_states) {
                    if (cave.overworld_x == player_x && cave.overworld_y == player_y) {
                        if (cave.is_sealed) {
                            turn_message = "The cave entrance is sealed.";
                            return turn_message;
                        } else {
                            overworld_player_x_return = player_x;
                            overworld_player_y_return = player_y;
                            current_level = GameLevel::DUNGEON;
                            initializeDungeonMap(player_x, player_y); 
                            turn_message = "You enter the dark cave.";
                            return turn_message;
                        }
                    }
                }
                turn_message = "Error: Cave data not found."; // Should not happen
                return turn_message;
            case T_SEALED_CAVE:
                turn_message = "The cave entrance is sealed.";
                return turn_message;
            case T_OASIS:
                hp = max_hp; // Heal to full
                game_map[player_y][player_x] = T_SAND; // Oasis turns to sand
                return "You rest at the oasis. HP restored! The oasis dries up.";
            case T_SAND:
                return "Desert sands stretch out.";
            case T_OVERWORLD_WALL: 
                return "A rocky outcrop blocks the way.";
            default:
                return "An unknown feature.";
        }
    } else { // GameLevel::DUNGEON
        char tile_char = dungeon_map[player_y][player_x];
        clearTurnMessage(); 
        switch (tile_char) {
            case T_STAIRS_UP:
                if (current_cave_ptr) {
                    if (current_cave_ptr->monsters_remaining_in_dungeon == 0) {
                        current_cave_ptr->is_sealed = true;
                        game_map[current_cave_ptr->overworld_y][current_cave_ptr->overworld_x] = T_SEALED_CAVE;
                        turn_message = "The cave echoes silently and seals behind you.";
                    } else {
                        current_cave_ptr->turns_until_monsters_emerge = (rand() % 4) + 2; // 2-5 turns
                        turn_message = "You feel an ominous presence as you leave the cave.";
                        // Monsters are still in current_cave_ptr->monsters_remaining_in_dungeon
                    }
                }
                current_level = GameLevel::OVERWORLD;
                player_x = overworld_player_x_return;
                player_y = overworld_player_y_return;
                // dungeon_map.clear(); // Optional: clear dungeon map
                // monsters.clear(); // Monsters for this dungeon are implicitly gone when player leaves
                current_cave_ptr = nullptr; // No longer in a specific cave
                return turn_message; // Return combined message
            case T_TREASURE_MAP_CHAR:
                player_attack += 1;
                max_hp += 1;
                hp += 1; // Heal 1, effectively gaining 1 current HP
                if (hp > max_hp) hp = max_hp;
                dungeon_map[player_y][player_x] = T_DUNGEON_FLOOR; // Treasure collected
                turn_message = "Found treasure! ATK +1, Max HP +1.";
                return turn_message;
            case T_DUNGEON_FLOOR:
                return "The air is damp and cool.";
            case T_DUNGEON_WALL:
                return "A cold, damp wall.";
            default:
                turn_message = "An odd fixture in the dungeon.";
                return turn_message;
        }
    }
}

String UltimaGame::getFormattedStats() {
    String stats_str = "LVL: " + String(level) + "\n";
    stats_str += "HP:  " + String(hp) + "/" + String(max_hp) + "\n";
    stats_str += "XP:  " + String(xp);
    return stats_str;
}

void UltimaGame::restartGame() {
    player_x = MAP_WIDTH / 2;
    player_y = MAP_HEIGHT / 2;
    current_level = GameLevel::OVERWORLD;
    initializeOverworldMap(); 
    initializeStats();       
    monsters.clear(); 
    overworld_monsters.clear(); // Clear overworld monsters
    clearTurnMessage();
    player_defeated_flag = false;
    current_cave_ptr = nullptr;
}

// New message handling methods
void UltimaGame::clearTurnMessage() {
    turn_message = "";
}

String UltimaGame::getTurnMessageAndClear() {
    String temp_msg = turn_message;
    turn_message = "";
    return temp_msg;
}

void UltimaGame::checkForLevelUp() {
    // Using a simple threshold for now: gain a level for every XP_PER_LEVEL earned.
    // Example: Level 1 needs 10 XP to get to Level 2. Level 2 needs 20 total XP to get to Level 3.
    // This means next_level_xp = current_level * XP_PER_LEVEL (to reach next level)
    // More robustly: player levels up if current_xp / XP_PER_LEVEL > (current_level -1)
    
    int potential_new_level = (xp / XP_PER_LEVEL) + 1;

    if (potential_new_level > level) {
        int levels_gained = potential_new_level - level;
        for (int i = 0; i < levels_gained; ++i) {
            level++;
            max_hp += 1;
            hp += 1; // Also heal 1 HP, effectively gaining 1 current HP unless already at max
            if (hp > max_hp) { // Ensure healing doesn't exceed new max
                hp = max_hp;
            }
            turn_message += "Level Up! You are now Level " + String(level) + ". Max HP +1. Hit +2%. ";
        }
    }
}

String UltimaGame::resolveCombat(Monster& monster) {
    String combat_log_segment = ""; 
    if (player_defeated_flag || !monster.active) {
        return ""; 
    }

    // 1. Player attacks monster
    float player_hit_roll = (float)rand() / RAND_MAX;
    // New hit chance calculation based on level
    float player_current_hit_chance = PLAYER_BASE_HIT_CHANCE + ((level - 1) * PLAYER_HIT_CHANCE_PER_LEVEL_INCREMENT);
    player_current_hit_chance = constrain(player_current_hit_chance, 0.0f, 1.0f); 

    if (player_hit_roll <= player_current_hit_chance) {
        monster.hp -= PLAYER_ATTACK_DAMAGE;
        combat_log_segment += "You hit Monster for " + String(PLAYER_ATTACK_DAMAGE) + " dmg. ";
    } else {
        combat_log_segment += "You missed Monster. ";
    }

    if (monster.hp <= 0) {
        monster.active = false;
        if (current_level == GameLevel::DUNGEON && current_cave_ptr && current_cave_ptr->monsters_remaining_in_dungeon > 0) {
             current_cave_ptr->monsters_remaining_in_dungeon--;
        } else if (monster.is_overworld_monster) {
            // Overworld monster defeated
        }
        xp += MONSTER_XP_REWARD;
        combat_log_segment += "Monster defeated! You gain " + String(MONSTER_XP_REWARD) + " XP. ";
        checkForLevelUp(); // Check for level up after gaining XP
        return combat_log_segment; 
    }

    // 2. Monster attacks player (if still active)
    float monster_hit_roll = (float)rand() / RAND_MAX;
    float monster_current_hit_chance = MONSTER_BASE_HIT_CHANCE + (player_moves_count * MONSTER_HIT_CHANCE_PER_PLAYER_MOVE_INCREMENT);
    monster_current_hit_chance = constrain(monster_current_hit_chance, 0.0f, 1.0f); 

    if (monster_hit_roll <= monster_current_hit_chance) {
        hp -= MONSTER_ATTACK_DAMAGE;
        combat_log_segment += "Monster hits you for " + String(MONSTER_ATTACK_DAMAGE) + " dmg.";
    } else {
        combat_log_segment += "Monster missed you.";
    }

    if (hp <= 0) {
        hp = 0;
        player_defeated_flag = true;
        combat_log_segment += " You have been defeated!";
    }
    
    return combat_log_segment; 
}

void UltimaGame::processCaveEvents() {
    // Process monster emergence countdowns
    for (auto& cave : cave_states) {
        if (cave.turns_until_monsters_emerge > 0) {
            cave.turns_until_monsters_emerge--;
            if (cave.turns_until_monsters_emerge == 0) {
                // Spawn overworld monster at cave entrance
                overworld_monsters.push_back(Monster(cave.overworld_x, cave.overworld_y, 2, 1, true));
                turn_message += "Monsters emerge from a cave! ";
            }
        }
    }

    // Check for win condition
    if (areAllCavesSealed()) {
        game_won_flag = true;
        turn_message = "YOU WIN ☻"; // U+263B
    }
}

bool UltimaGame::areAllCavesSealed() const {
    // If there are no caves, player hasn't won yet
    if (cave_states.empty()) {
        return false;
    }
    
    // Check if all caves are sealed
    for (const auto& cave : cave_states) {
        if (!cave.is_sealed) {
            return false;
        }
    }
    return true;
}

void UltimaGame::moveMonsters() {
    if (player_defeated_flag) return;

    if (current_level == GameLevel::DUNGEON) {
        String monster_actions_msg = ""; 
        for (auto& monster : monsters) { // These are dungeon monsters
            if (!monster.active) continue;
            
            int sight_range = (rand() % 7) + 3; // Random sight range 3-9 tiles (inclusive: rand()%7 gives 0-6, add 3)
            int dist_to_player_x = abs(player_x - monster.x);
            int dist_to_player_y = abs(player_y - monster.y);

            Serial.printf("[UltimaGame] Monster at (%d,%d) checking player at (%d,%d)\n", 
                monster.x, monster.y, player_x, player_y);
            Serial.printf("[UltimaGame] Distance: dx=%d, dy=%d, sight_range=%d\n", 
                dist_to_player_x, dist_to_player_y, sight_range);

            if (dist_to_player_x <= sight_range && dist_to_player_y <= sight_range) { // Player is visible
                int dx = 0, dy = 0;
                if (player_x < monster.x) dx = -1;
                else if (player_x > monster.x) dx = 1;
                if (player_y < monster.y) dy = -1;
                else if (player_y > monster.y) dy = 1;

                Serial.printf("[UltimaGame] Player visible! Moving dx=%d, dy=%d\n", dx, dy);

                bool moved = false;
                // Try X movement first
                if (dx != 0) {
                    int next_monster_x = monster.x + dx;
                    int next_monster_y = monster.y;
                    
                    if (next_monster_x == player_x && next_monster_y == player_y) {
                        monster_actions_msg += resolveCombat(monster);
                        if (player_defeated_flag) break;
                        continue;
                    } else if (dungeon_map[next_monster_y][next_monster_x] == T_DUNGEON_FLOOR) {
                        bool occupied_by_other_dungeon_monster = false;
                        for (const auto& other_monster : monsters) {
                            if (&other_monster != &monster && other_monster.active && 
                                other_monster.x == next_monster_x && other_monster.y == next_monster_y) {
                                occupied_by_other_dungeon_monster = true;
                                break;
                            }
                        }
                        if (!occupied_by_other_dungeon_monster) {
                            monster.x = next_monster_x;
                            moved = true;
                        }
                    }
                }

                // If X movement didn't work, try Y movement
                if (!moved && dy != 0) {
                    int next_monster_x = monster.x;
                    int next_monster_y = monster.y + dy;
                    
                    if (next_monster_x == player_x && next_monster_y == player_y) {
                        monster_actions_msg += resolveCombat(monster);
                        if (player_defeated_flag) break;
                        continue;
                    } else if (dungeon_map[next_monster_y][next_monster_x] == T_DUNGEON_FLOOR) {
                        bool occupied_by_other_dungeon_monster = false;
                        for (const auto& other_monster : monsters) {
                            if (&other_monster != &monster && other_monster.active && 
                                other_monster.x == next_monster_x && other_monster.y == next_monster_y) {
                                occupied_by_other_dungeon_monster = true;
                                break;
                            }
                        }
                        if (!occupied_by_other_dungeon_monster) {
                            monster.y = next_monster_y;
                        }
                    }
                }
            }
        }
        if (!monster_actions_msg.isEmpty()) {
            if (!turn_message.isEmpty() && !turn_message.endsWith(" ")) turn_message += " ";
            turn_message += monster_actions_msg;
        }
    } else if (current_level == GameLevel::OVERWORLD) {
        String monster_actions_msg = "";
        for (auto& monster : overworld_monsters) {
            if (!monster.active) continue;

            int sight_range = (rand() % 7) + 3; // Random sight range 3-9 tiles (inclusive: rand()%7 gives 0-6, add 3)
            int dist_to_player_x = abs(player_x - monster.x);
            int dist_to_player_y = abs(player_y - monster.y);

            if (dist_to_player_x <= sight_range && dist_to_player_y <= sight_range) { 
                int dx = 0, dy = 0;
                if (player_x < monster.x) dx = -1;
                else if (player_x > monster.x) dx = 1;
                if (player_y < monster.y) dy = -1;
                else if (player_y > monster.y) dy = 1;

                // Try both X and Y movement
                bool moved = false;
                
                // Try X movement first
                if (dx != 0) {
                    int next_monster_x = monster.x + dx;
                    int next_monster_y = monster.y;
                    
                    if (next_monster_x == player_x && next_monster_y == player_y) {
                        monster_actions_msg += resolveCombat(monster);
                        if (player_defeated_flag) break;
                        continue;
                    } else if (game_map[next_monster_y][next_monster_x] == T_SAND || game_map[next_monster_y][next_monster_x] == T_OASIS) {
                        bool occupied = false;
                        for (const auto& om : overworld_monsters) {
                            if (&om != &monster && om.active && om.x == next_monster_x && om.y == next_monster_y) {
                                occupied = true;
                                break;
                            }
                        }
                        if (!occupied) {
                            monster.x = next_monster_x;
                            moved = true;
                        }
                    }
                }

                // If X movement didn't work, try Y movement
                if (!moved && dy != 0) {
                    int next_monster_x = monster.x;
                    int next_monster_y = monster.y + dy;
                    
                    if (next_monster_x == player_x && next_monster_y == player_y) {
                        monster_actions_msg += resolveCombat(monster);
                        if (player_defeated_flag) break;
                        continue;
                    } else if (game_map[next_monster_y][next_monster_x] == T_SAND || game_map[next_monster_y][next_monster_x] == T_OASIS) {
                        bool occupied = false;
                        for (const auto& om : overworld_monsters) {
                            if (&om != &monster && om.active && om.x == next_monster_x && om.y == next_monster_y) {
                                occupied = true;
                                break;
                            }
                        }
                        if (!occupied) {
                            monster.y = next_monster_y;
                        }
                    }
                }
            }
            if (player_defeated_flag) break;
        }
        if (!monster_actions_msg.isEmpty()) {
             if (!turn_message.isEmpty() && !turn_message.endsWith(" ")) turn_message += " ";
            turn_message += monster_actions_msg;
        }
    }
    processCaveEvents(); // Process cave events after all monster movements for the turn
} 