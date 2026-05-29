# MOD-PLAYERBOTS

## Repo & Setup

- **Separate git repo**: `origin` = `liyunfan1223/mod-playerbots`, `fork` = `avirar/mod-playerbots`
- **Requires custom AC fork**: Uses `mod-playerbots/azerothcore-wotlk` `Playerbot` branch, NOT standard AzerothCore
- **Installation**: clone the custom fork, then clone this module into `modules/mod-playerbots`
- **PRs go to `test-staging`** branch, not `master`

## Architecture

### Engine-Strategy-Action-Trigger-Value

Each bot has **3 engines**: `BOT_STATE_COMBAT`, `BOT_STATE_NON_COMBAT`, `BOT_STATE_DEAD`. Engines run strategies that queue actions. Triggers fire when conditions are met. Values provide context-aware data.

```
Engine → Strategy(s) → Action(s)   (triggered by Trigger, evaluated via Value)
```

### Key Singletons

| Singleton | Class | Purpose |
|-----------|-------|---------|
| `sPlayerbotAIConfig` | `PlayerbotAIConfig` | All config from `playerbots.conf.dist` |
| `sRandomPlayerbotMgr` | `RandomPlayerbotMgr` | Random bot lifecycle, BG/Arena/LFG queues, PID activity scaling |
| `sPlayerbotsMgr` | `PlayerbotsMgr` | BotAI/Mgr lookup per player |
| `PlayerbotWorldThreadProcessor` | - | Thread-safe world ops for bots |

### Registration Pattern

Each class/instance has an `*AiObjectContext` file that binds its Actions/Strategies/Triggers via `AddAction()`, `AddStrategy()`, `AddTrigger()` calls. This is the critical wiring file for adding new behaviors.

### Class AI

10 classes, each with its own directory under `Ai/Class/`:
```
Dk/ Druid/ Hunter/ Mage/ Paladin/ Priest/ Rogue/ Shaman/ Warlock/ Warrior/
```
Each has `Action/`, `Strategy/`, `Trigger/` subdirs plus `*AiObjectContext.cpp` (registration). Paladin also has `Util/`.

### Instance AI

- **Dungeons**: AuchenaiCrypts, AzjolNerub, CullingOfStratholme, DraktharonKeep, ForgeOfSouls, Gundrak, HallsOfLightning, HallsOfReflection, HallsOfStone, Nexus, Oculus, OldKingdom, PitOfSaron, TrialOfTheChampion, UtgardeKeep, UtgardePinnacle, VioletHold + TBC/WotLK context files
- **Raids**: Aq20, BlackTemple, BlackwingLair, EyeOfEternity, GruulsLair, HyjalSummit, Icecrown, Karazhan, Magtheridon, MoltenCore, Naxxramas, ObsidianSanctum, Onyxia, SerpentshrineCavern, TempestKeep, Ulduar, VaultOfArchavon, ZulAman

## Key Source Files

| File | Purpose |
|------|---------|
| `Bot/PlayerbotAI.h/.cpp` | Core AI per bot character, owns 3 engines + AiObjectContext |
| `Bot/PlayerbotMgr.h/.cpp` | `PlayerbotHolder` base, `PlayerbotMgr` (per-player), `PlayerbotsMgr` (global singleton) |
| `Bot/RandomPlayerbotMgr.h/.cpp` | Random bot lifecycle, PID controller, BG/Arena/LFG queues |
| `Bot/Factory/PlayerbotFactory.h/.cpp` | Bot creation: equip, talents, spells, skills, professions, glyphs, pets, mounts, bags, quests |
| `Bot/Factory/AiFactory.h/.cpp` | Creates AiObjectContext + Engine instances per bot |
| `Bot/Factory/RandomPlayerbotFactory.h/.cpp` | Creates random bot characters, names, guilds, arena teams |
| `PlayerbotAIConfig.h/.cpp` | Config singleton (2442-line .conf.dist) |
| `Script/Playerbots.cpp` | Main script: DB loader, player/world/server/unit hooks, command handler |

### PlayerbotFactory Key Methods

```cpp
void Randomize(bool incremental);           // Full bot randomization
void InitEquipment(bool incremental, ...);  // Gear selection
uint32 InitTalentsTree(...);                // Talent tree initialization
void InitSkills();                          // Skill initialization
void InitTradeSkills();                     // Profession setup
void InitAvailableSpells();                 // Spell learning
void InitGlyphs(bool increment);            // Glyph setup
void InitMounts();                          // Mount collection
void InitPet();                             // Hunter/Warlock pet
void ApplyEnchantAndGemsNew(...);           // Enchant + gem sockets
```

## Data Tools

All `acore_data_*` tools work with playerbots tables (31 total, listed under `sql_auxiliary`):

```bash
# Query playerbots data
acore_data_sql "SELECT * FROM playerbots_random_bots WHERE bot = 47401"
acore_data_query name=PlayerbotsEnchants filter={"class":1} 
acore_data_lookup query=PlayerbotsRandomBots
acore_data_list search=playerbot
```

### Key Playerbots Tables

| Table | Purpose |
|-------|---------|
| `playerbots_random_bots` | Bot event/value cache (level, spec, randomize) |
| `playerbots_enchants` | Enchant templates per class/spec/slot |
| `playerbots_weightscales` / `playerbots_weightscale_data` | Item stat weights per class |
| `playerbots_item_info_cache` | Cached item info for bot gear scoring |
| `playerbots_equip_cache` | Cached bot equipment |
| `playerbots_rarity_cache` / `playerbots_rnditem_cache` / `playerbots_tele_cache` | Bot state caches |
| `ai_playerbot_texts` / `ai_playerbot_texts_chance` | Bot speech/dialogue (localized, 1758 lines) |
| `playerbots_travelnode*` (3 tables) | Travel path/routing data |
| `playerbots_custom_strategy` | Per-class custom strategy overrides |
| `playerbots_guild_tasks` | Guild task system |
| `playerbots_preferred_mounts` | Mount preferences per class |
| `playerbots_dungeon_suggestion_*` (3 tables) | Dungeon suggestion system |
| `playerbots_names` / `playerbots_guild_names` / `playerbots_arena_team_names` | Name pools |
| `playerbots_rpg_races` | RPG race settings |
| `playerbots_speech` / `playerbots_speech_probability` | Speech system |
| `playerbots_account_type` / `playerbots_account_links` / `playerbots_account_keys` | Account management |
| `playerbots_db_store` | Generic key-value storage |

Additionally modifies `acore_characters` (bot names/guild names) and `acore_world` (RPG races, DBC override data).

## Development Patterns

### Adding a New Action

```cpp
class MyNewAction : public Action
{
public:
    MyNewAction(PlayerbotAI* botAI) : Action(botAI, "my_new_action") {}

    bool Execute(ActionEvent event) override
    {
        // implement behavior
        return true;
    }

    bool isUseful() override { return true; }
};
```

Register in the relevant `*ActionContext.h`:
```cpp
contexts->Add(new ActionCreator<MyNewAction>("my_new_action"));
```

### Adding a New Strategy

Create a class inheriting `Strategy`, add trigger registrations in its constructor, then register in the `*StrategyContext.h`.

### Adding a New Trigger

Create a class inheriting `Trigger`, implement `IsActive()`, register in the `*TriggerContext.h`.

### SQL Updates

- Playerbots DB updates: `data/sql/playerbots/updates/` (module repo)
- Characters DB updates: `data/sql/characters/`
- World DB updates: `data/sql/world/`

### Code Style

- `.clang-format`: Google-based, IndentWidth: 4, ColumnLimit: 120, BreakBeforeBraces: Allman, PointerAlignment: Left
- CI enforces via `code_style.yml` (clang-format) and `codestyle_cpp.yml` (Python checker)
- PR template emphasizes: **stability > performance > predictability > behavioral realism**

### No Tests

The module has no unit test infrastructure. Changes are verified via build + manual testing.

### Design Principles (from PR template)

- Default behavior must be cheap; expensive behavior must be opt-in
- Small logic increases scale poorly across thousands of bots
- Bots don't need to behave perfectly — believable is the goal
- New bot dialogue must use `GetBotTextOrDefault` with translatable SQL updates
