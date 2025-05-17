#pragma once

#include <Arduino.h>
#include <vector>

#define MAP_WIDTH 20
#define MAP_HEIGHT 15
#define VIEW_WIDTH 21  // Adjusted for unscii_8 font and side stats panel
#define VIEW_HEIGHT 15 // Adjusted for unscii_8 font and bottom message log

// Tile Definitions (Chars)
const char T_SAND = '.';
const char T_OASIS = '~';
const char T_CAVE_ENTRANCE = 'O'; // Overworld representation of a cave
const char T_DUNGEON_WALL = '#';
const char T_DUNGEON_FLOOR = ' ';
const char T_STAIRS_UP = '<';   // Leads from dungeon to overworld
const char T_STAIRS_DOWN = '>'; // Leads from overworld to dungeon (player appears on this in dungeon)
const char T_PLAYER = '@';
const char T_OVERWORLD_WALL = '#'; // Existing wall type, now specific to overworld boundary

// Game Level Management
enum class GameLevel {
    OVERWORLD,
    DUNGEON
};

class UltimaGame {
public:
    UltimaGame();

    void movePlayer(int dx, int dy);
    String renderView(); // Renders a portion of the map around the player

    int getPlayerX() const { return player_x; }
    int getPlayerY() const { return player_y; }

    // Player Stats Getters
    int getHP() const { return hp; }
    int getMaxHP() const { return max_hp; }
    int getLevel() const { return level; }
    int getXP() const { return xp; }

    String searchCurrentTile(); // New method to search the player's current tile
    String getFormattedStats(); // Method to get stats as a displayable string

private:
    std::vector<String> game_map; // Represents the overworld
    std::vector<String> dungeon_map; // Represents the current dungeon level
    GameLevel current_level;

    int player_x;
    int player_y;
    int overworld_player_x_return; // To store player's X on overworld when entering dungeon
    int overworld_player_y_return; // To store player's Y on overworld when entering dungeon

    // Player Stats
    int hp;
    int max_hp;
    int level;
    int xp;

    void initializeOverworldMap(); // Renamed from initializeMap
    void initializeDungeonMap(int from_cave_x, int from_cave_y); // For generating a new dungeon
    void initializeStats();
}; 