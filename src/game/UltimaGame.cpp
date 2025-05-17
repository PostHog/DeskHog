#include "UltimaGame.h"
#include <cstdlib> // For rand()
#include <ctime>   // For time() for seeding rand()

UltimaGame::UltimaGame() : player_x(MAP_WIDTH / 2), player_y(MAP_HEIGHT / 2), current_level(GameLevel::OVERWORLD) {
    std::srand(std::time(0)); // Seed random number generator
    initializeOverworldMap();
    initializeStats(); // Initialize player stats
}

void UltimaGame::initializeStats() {
    hp = 100;
    max_hp = 100;
    level = 1;
    xp = 0;
}

void UltimaGame::initializeOverworldMap() { // Renamed from initializeMap
    game_map.clear();
    for (int i = 0; i < MAP_HEIGHT; ++i) {
        String row = "";
        for (int j = 0; j < MAP_WIDTH; ++j) {
            if (i == 0 || i == MAP_HEIGHT - 1 || j == 0 || j == MAP_WIDTH - 1) {
                row += T_OVERWORLD_WALL; // Use defined constant for boundary
            } else {
                int r = rand() % 100; // Percentage roll
                if (r < 5) { // 5% chance for a Cave Entrance
                    row += T_CAVE_ENTRANCE;
                } else if (r < 10) { // 5% chance for an Oasis (total 10% for features)
                    row += T_OASIS;
                } else {
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

void UltimaGame::initializeDungeonMap(int from_cave_x, int from_cave_y) {
    dungeon_map.clear();
    // For simplicity, dungeon is same size as overworld map, but we'll only use a small part.
    // This simple dungeon is a 5x5 room in the center of the dungeon_map array.
    // Player starts at (center_x, center_y + 1) on STAIRS_DOWN.
    // STAIRS_UP are at (center_x, center_y - 1).

    int room_size = 5;
    int room_start_x = (MAP_WIDTH - room_size) / 2;
    int room_start_y = (MAP_HEIGHT - room_size) / 2;

    for (int i = 0; i < MAP_HEIGHT; ++i) {
        String row = "";
        for (int j = 0; j < MAP_WIDTH; ++j) {
            // Check if current (j,i) is within the 5x5 room boundaries
            if (j >= room_start_x && j < room_start_x + room_size &&
                i >= room_start_y && i < room_start_y + room_size) {
                // Current cell is inside the room area
                if (j == room_start_x || j == room_start_x + room_size - 1 ||
                    i == room_start_y || i == room_start_y + room_size - 1) {
                    // Cell is on the border of the room - make it a wall
                    row += T_DUNGEON_WALL;
                } else {
                    // Cell is inside the room, not on the border - make it floor
                    row += T_DUNGEON_FLOOR;
                }
            } else {
                // Outside the 5x5 room, fill with solid rock (or unused space, effectively wall)
                row += T_DUNGEON_WALL; // Treat area outside room as wall for now
            }
        }
        dungeon_map.push_back(row);
    }

    // Place Stairs Up (leads to overworld at from_cave_x, from_cave_y)
    // Centered in the room, at the "top" relative to player start.
    dungeon_map[room_start_y + 1][room_start_x + room_size / 2] = T_STAIRS_UP;

    // Place Stairs Down (where player arrives)
    // Centered in the room, at the "bottom" relative to stairs up.
    int player_dungeon_start_x = room_start_x + room_size / 2;
    int player_dungeon_start_y = room_start_y + room_size - 2; // One tile above bottom wall
    dungeon_map[player_dungeon_start_y][player_dungeon_start_x] = T_STAIRS_DOWN;

    // Update player position to the dungeon entry point
    player_x = player_dungeon_start_x;
    player_y = player_dungeon_start_y;
}

void UltimaGame::movePlayer(int dx, int dy) {
    int new_x = player_x + dx;
    int new_y = player_y + dy;

    char target_tile;
    if (current_level == GameLevel::OVERWORLD) {
        if (new_x >= 0 && new_x < MAP_WIDTH && new_y >= 0 && new_y < MAP_HEIGHT) {
            target_tile = game_map[new_y][new_x];
            if (target_tile != T_OVERWORLD_WALL) { // Passable: Sand, Oasis, Cave Entrance (interaction handled by search)
                player_x = new_x;
                player_y = new_y;
            }
        }
    } else { // GameLevel::DUNGEON
        if (new_x >= 0 && new_x < MAP_WIDTH && new_y >= 0 && new_y < MAP_HEIGHT) {
            target_tile = dungeon_map[new_y][new_x];
            if (target_tile == T_DUNGEON_FLOOR || target_tile == T_STAIRS_UP || target_tile == T_STAIRS_DOWN) {
                player_x = new_x;
                player_y = new_y;
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
            } else if (map_render_x >= 0 && map_render_x < MAP_WIDTH &&
                       map_render_y >= 0 && map_render_y < MAP_HEIGHT) {
                char tile = (*current_map_ptr)[map_render_y][map_render_x];
                view_str += tile;
            } else {
                view_str += ' '; // Out of map bounds, show empty space
            }
        }
        if (y_offset < VIEW_HEIGHT - 1) {
             view_str += "\n";
        }
    }
    return view_str;
}

String UltimaGame::searchCurrentTile() {
    char tile_char;
    if (current_level == GameLevel::OVERWORLD) {
        tile_char = game_map[player_y][player_x];
        switch (tile_char) {
            case T_CAVE_ENTRANCE:
                overworld_player_x_return = player_x;
                overworld_player_y_return = player_y;
                current_level = GameLevel::DUNGEON;
                initializeDungeonMap(player_x, player_y); // Player coords updated inside this func
                return "You enter the dark cave.";
            case T_OASIS:
                hp = max_hp; // Heal to full
                return "You rest at the oasis. HP restored!";
            case T_SAND:
                return "Desert sands stretch out.";
            case T_OVERWORLD_WALL: // Should not happen if movePlayer is correct
                return "A rocky outcrop blocks the way.";
            default:
                return "An unknown feature.";
        }
    } else { // GameLevel::DUNGEON
        tile_char = dungeon_map[player_y][player_x];
        switch (tile_char) {
            case T_STAIRS_UP:
                current_level = GameLevel::OVERWORLD;
                player_x = overworld_player_x_return;
                player_y = overworld_player_y_return;
                return "You climb the stairs back to the desert.";
            case T_STAIRS_DOWN: // Standing on the entry stairs
                return "These stairs brought you here.";
            case T_DUNGEON_FLOOR:
                return "The air is damp and cool.";
            case T_DUNGEON_WALL:
                return "A cold, damp wall.";
            default:
                return "An odd fixture in the dungeon.";
        }
    }
}

String UltimaGame::getFormattedStats() {
    String stats_str = "LVL: " + String(level) + "\n";
    stats_str += "HP:  " + String(hp) + "/" + String(max_hp) + "\n";
    stats_str += "XP:  " + String(xp);
    return stats_str;
} 