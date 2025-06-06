#pragma once

#include <Arduino.h>
#include <vector>

#define MAP_WIDTH 20
#define MAP_HEIGHT 15
#define VIEW_WIDTH 9  // Adjusted for unscii_16 font (145px / 8px_char = ~18, using 17 for odd)
#define VIEW_HEIGHT 5 // Adjusted for unscii_16 font (111px / 16px_char = ~6, using 5 for odd)

// Tile Definitions (Chars)
const char T_SAND = '.';
const char T_OASIS = '~';
const char T_OPEN_CAVE = 'O';       // New: Open cave entrance
const char T_SEALED_CAVE = 'X';     // New: Sealed cave entrance
const char T_DUNGEON_WALL = '#';
const char T_DUNGEON_FLOOR = ' ';
const char T_STAIRS_UP = '<';   // Leads from dungeon to overworld
const char T_TREASURE_MAP_CHAR = 'T'; // Character to store on the map for treasure
const char T_OVERWORLD_WALL = '#'; // Existing wall type, now specific to overworld boundary

// Custom Game Tile Definitions (UTF-8 Strings)
const char T_PLAYER[] = "\xEE\xB0\xA8"; // Person symbol
const char T_MONSTER[] = "\xEE\xB0\xB4"; // Monster symbol U+EC34 (Assumed UTF-8, placeholder if incorrect)
const char T_TREASURE_SYMBOL[] = "\xD8\xAB"; // U+062B Arabic Letter Alef With Madda Above (looks treasure-like in unscii)

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
    int getPlayerAttack() const { return player_attack; } // New getter
    int getPlayerMovesCount() const { return player_moves_count; } // Getter for player moves

    String searchCurrentTile(); // New method to search the player's current tile
    String getFormattedStats(); // Method to get stats as a displayable string
    void restartGame(); // New: Method to restart the game

    // Monster-related methods
    void moveMonsters();
    String getTurnMessageAndClear(); // Gets the accumulated message for the turn
    void clearTurnMessage(); // Clears the turn message
    bool isPlayerDefeated() const { return player_defeated_flag; }
    bool isGameWon() const { return game_won_flag; } // New: Check if player has won
    bool areAllCavesSealed() const; // New: Check if all caves are sealed

private:
    std::vector<String> game_map; // Represents the overworld
    std::vector<String> dungeon_map; // Represents the current dungeon level
    GameLevel current_level;
    bool game_won_flag; // New: Flag to indicate if player has won

    struct CaveState {
        int overworld_x, overworld_y;
        bool is_sealed;
        int monsters_remaining_in_dungeon; // How many were left when player exited
        int turns_until_monsters_emerge; // Countdown for emergence, -1 if not active

        CaveState(int ox, int oy) : 
            overworld_x(ox), overworld_y(oy), is_sealed(false), 
            monsters_remaining_in_dungeon(0), turns_until_monsters_emerge(-1) {}
    };
    std::vector<CaveState> cave_states; // Tracks all discovered caves
    CaveState* current_cave_ptr; // Pointer to the state of the cave the player is currently in or just exited

    struct Monster {
        int x, y;
        int hp;
        int max_hp;
        int attack;
        bool active;
        bool is_overworld_monster; // New: Flag for overworld monsters

        Monster(int start_x, int start_y, int start_hp, int start_attack, bool on_overworld = false) :
            x(start_x), y(start_y), hp(start_hp), max_hp(start_hp), attack(start_attack), active(true), is_overworld_monster(on_overworld) {}
    };
    std::vector<Monster> monsters; // Dungeon monsters
    std::vector<Monster> overworld_monsters; // New: Monsters on the overworld map
    String turn_message; // Accumulates messages for the current turn
    bool player_defeated_flag; // Flag to indicate if player has been defeated
    int player_moves_count; // Tracks total player moves

    int player_x;
    int player_y;
    int overworld_player_x_return; // To store player's X on overworld when entering dungeon
    int overworld_player_y_return; // To store player's Y on overworld when entering dungeon

    // Player Stats
    int hp;
    int max_hp;
    int level;
    int xp;
    int player_attack; // New: Player's attack power

    void initializeOverworldMap(); // Renamed from initializeMap
    void initializeDungeonMap(int from_cave_x, int from_cave_y); // For generating a new dungeon
    void initializeStats();

    // Constants for Cellular Automata
    static const int DUNGEON_GENERATION_ITERATIONS = 5; // How many times to run the CA simulation
    static const int WALL_THRESHOLD = 5; // If a cell has this many or more wall neighbours, it becomes a wall
    static const float INITIAL_FLOOR_CHANCE; // 45% chance for a tile to be initially floor
    static const int MIN_DUNGEON_FLOOR_TILES = 25; // Minimum number of floor tiles for a valid dungeon
    static const int MAX_DUNGEON_GENERATION_ATTEMPTS = 5; // Max attempts to generate a suitable dungeon

    // Monster Stat & Spawning Constants
    static const int MONSTER_ATTACK_DAMAGE = 1;
    static constexpr float MONSTER_BASE_HIT_CHANCE = 0.45f;
    static constexpr float MONSTER_HIT_CHANCE_PER_PLAYER_MOVE_INCREMENT = 0.0001f; // 0.01%
    static const int MONSTER_XP_REWARD = 4;
    static const int MAX_MONSTERS_PER_DUNGEON = 3;

    // Player Combat Stats
    static const int PLAYER_ATTACK_DAMAGE = 1;
    static constexpr float PLAYER_BASE_HIT_CHANCE = 0.55f;
    static constexpr float PLAYER_HIT_CHANCE_PER_LEVEL_INCREMENT = 0.02f; // 2% per level (after level 1)
    static const int XP_PER_LEVEL = 10; // XP needed for each level up

    // Dungeon Feature Constants
    // static constexpr float DUNGEON_TREASURE_CHANCE = 0.05f; // 5% chance per floor tile -- To be removed
    static constexpr float CAVE_HAS_TREASURE_CHANCE = 0.10f; // 10% chance for a cave to contain treasure

    struct Point { 
        int x, y;
        // Constructor for easy initialization
        Point(int _x = 0, int _y = 0) : x(_x), y(_y) {}
        // Equality operator for comparing points, useful for std::vector::erase or std::find
        bool operator==(const Point& other) const {
            return x == other.x && y == other.y;
        }
    };

    // Helper methods for dungeon generation
    void runCellularAutomataIteration(std::vector<String>& map_to_smooth);
    int countAliveNeighbors(const std::vector<String>& map_to_check, int x, int y, char wall_tile, char floor_tile);
    void findLargestConnectedArea(std::vector<Point>& out_largest_area_tiles, std::vector<String>& map_to_analyze, char floor_tile);
    void floodFill(int x, int y, const std::vector<String>& map_to_fill, std::vector<Point>& current_area_tiles, std::vector<std::vector<bool>>& visited, char floor_tile);

    // Combat
    String resolveCombat(Monster& monster);
    void checkForLevelUp(); // New: Method to check and process level ups

    // Cave Events
    void processCaveEvents(); // New: To handle monster emergence and cave sealing over time
}; 