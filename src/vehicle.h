#ifndef VEHICLE_H
#define VEHICLE_H

#include "calendar.h"
#include "tileray.h"
#include "color.h"
#include "cursesdef.h" // WINDOW
#include "damage.h"
#include "item.h"
#include "line.h"
#include "item_stack.h"
#include "active_item_cache.h"
#include "string_id.h"
#include "units.h"

#include <vector>
#include <array>
#include <map>
#include <list>
#include <string>
#include <iosfwd>

class map;
class player;
class vehicle;
class vpart_info;
enum vpart_bitflags : int;
using vpart_id = string_id<vpart_info>;
struct vehicle_prototype;
using vproto_id = string_id<vehicle_prototype>;

//collision factor for vehicle-vehicle collision; delta_v in mph
float get_collision_factor(float delta_v);

//How far to scatter parts from a vehicle when the part is destroyed (+/-)
constexpr int SCATTER_DISTANCE = 3;
constexpr int k_mvel = 200; //adjust this to balance collision damage

struct fuel_type {
    /** Id of the item type that represents the fuel. It may not be valid for certain pseudo
     * fuel types like muscle. */
    itype_id id;
    /** See @ref vehicle::consume_fuel */
    int coeff;
};

const std::array<fuel_type, 7> &get_fuel_types();

enum veh_coll_type : int {
    veh_coll_nothing,  // 0 - nothing,
    veh_coll_body,     // 1 - monster/player/npc
    veh_coll_veh,      // 2 - vehicle
    veh_coll_bashable, // 3 - bashable
    veh_coll_other,    // 4 - other
    num_veh_coll_types
};

struct veh_collision {
    //int veh?
    int           part        = 0;
    veh_coll_type type        = veh_coll_nothing;
    int           imp         = 0; // impulse
    void         *target      = nullptr;  //vehicle
    int           target_part = 0; //veh partnum
    std::string   target_name;

    veh_collision() = default;
};

class vehicle_stack : public item_stack {
private:
    point location;
    vehicle *myorigin;
    int part_num;
public:
vehicle_stack( std::list<item> *newstack, point newloc, vehicle *neworigin, int part ) :
    item_stack( newstack ), location( newloc ), myorigin( neworigin ), part_num( part ) {};
    std::list<item>::iterator erase( std::list<item>::iterator it ) override;
    void push_back( const item &newitem ) override;
    void insert_at( std::list<item>::iterator index, const item &newitem ) override;
    int count_limit() const override {
        return MAX_ITEM_IN_VEHICLE_STORAGE;
    }
    units::volume max_volume() const override;
};

/**
 * Structure, describing vehicle part (ie, wheel, seat)
 */
struct vehicle_part : public JsonSerializer, public JsonDeserializer
{
    friend vehicle;
    friend class veh_interact;
    friend visitable<vehicle_cursor>;
    friend item_location;
    friend class turret_data;

    enum : int { passenger_flag = 1 };

    vehicle_part(); /** DefaultConstructible */

    vehicle_part( const vpart_id& vp, int dx, int dy, item&& it );

    /** Check this instance is non-null (not default constructed) */
    explicit operator bool() const;

    bool has_flag(int const flag) const noexcept { return flag & flags; }
    int  set_flag(int const flag)       noexcept { return flags |= flag; }
    int  remove_flag(int const flag)    noexcept { return flags &= ~flag; }

    /** Translated name of a part inclusive of any current status effects */
    std::string name() const;

    /** Specific type of fuel, charges or ammunition currently contained by a part */
    itype_id ammo_current() const;

    /** Maximum amount of fuel, charges or ammunition that can be contained by a part */
    long ammo_capacity() const;

    /** Amount of fuel, charges or ammunition currently contained by a part */
    long ammo_remaining() const;

    /**
     * Set fuel, charges or ammunition for this part removing any existing ammo
     * @param ammo specific type of ammo (must be compatible with vehicle part)
     * @param qty maximum ammo (capped by part capacity) or negative to fill to capacity
     * @return amount of ammo actually set or negative on failure
     */
    int ammo_set( const itype_id &ammo, long qty = -1 );

    /** Remove all fuel, charges or ammunition (if any) from this part */
    void ammo_unset();

    /**
     * Consume fuel, charges or ammunition (if available)
     * @param qty maximum amount of ammo that should be consumed
     * @param pos current global location of part from which ammo is being consumed
     * @return amount consumed which will be between 0 and @ref qty
     */
    long ammo_consume( long qty, const tripoint& pos );

    /* Can part in current state be reloaded optionally with specific @ref obj */
    bool can_reload( const itype_id &obj = "" ) const;

    /** Inspect contents of tank or @return empty list if part is not a vehicle tank */
    const std::list<item>& contents() const;

    /**
     *  Try adding @param liquid to tank optionally limited by @param qty
     *  @return whether any of the liquid was consumed (which may be less than qty)
     */
    bool fill_with( item &liquid, long qty = LONG_MAX );

    /**
     *  Try to drain liquid from a tank
     *  @param qty number of charges to drain or negative to drain the entire tank
     *  @return drained liquid or null item if tank empty or part was not a tank
     */
    item drain( long qty = -1 );

    /** Current faults affecting this part (if any) */
    const std::set<fault_id>& faults() const;

    /** Faults which could potentially occur with this part (if any) */
    std::set<fault_id> faults_potential() const;

    /** Try to set fault returning false if specified fault cannot occur with this item */
    bool fault_set( const fault_id &f );

    /** Get wheel diameter times wheel width (inches^2) or return 0 if part is not wheel */
    int wheel_area() const;

    /** Get wheel diameter (inches) or return 0 if part is not wheel */
    int wheel_diameter() const;

    /** Get wheel width (inches) or return 0 if part is not wheel */
    int wheel_width() const;

    /** Efficiency (0.0,1.0] at which engine converts fuel energy to output power at @ref rpm */
    float efficiency( int rpm ) const;

    /** Effective power output (watts) of part optionally adjusted for effects of damage or faults */
    int power( bool effects = true ) const;

    /**
     *  Get NPC currently assigned to this part (seat, turret etc)?
     *  @note checks crew member is alive and currently allied to the player
     *  @return nullptr if no valid crew member is currently assigned
     */
    npc *crew() const;

    /** Set crew member for this part (seat, truret etc) who must be a player ally)
     *  @return true if part can have crew members and passed npc was suitable
     */
    bool set_crew( const npc &who );

    /** Remove any currently assigned crew member for this part */
    void unset_crew();

    /**
     * @name Part capabilities
     *
     * A part can provide zero or more capabilities. Some capabilities are mutually
     * exclusive, for example a part cannot be both a fuel tank and battery
     */
    /*@{*/

    /** Can this part provide power or propulsion? */
    bool is_engine() const;

    /** Can this part generate electrical power when attached to a running engine? */
    bool is_alternator() const;

    /** Is this any type of vehicle light? */
    bool is_light() const;

    /** Can this part contain liquid fuels? */
    bool is_tank() const;

    /** Can this part store electrical charge? */
    bool is_battery() const;

    /** Is this part a reactor? */
    bool is_reactor() const;

    /** Can this part function as a turret? */
    bool is_turret() const;

    /** Can a player or NPC use this part as a seat? */
    bool is_seat() const;

    /*@}*/

public:
    /** mount point: x is on the forward/backward axis, y is on the left/right axis */
    point mount;

    /** mount translated to face.dir [0] and turn_dir [1] */
    std::array<point, 2> precalc = { { point( -1, -1 ), point( -1, -1 ) } };

    /** current part health with range [0,durability] */
    int hp() const;

    /** parts are considered broken at zero health */
    bool is_broken() const { return base.damage() >= base.max_damage(); }

    int blood        = 0;         // how much blood covers part (in turns).
    bool inside      = false;     // if tile provides cover. WARNING: do not read it directly, use vehicle::is_inside() instead
    bool removed     = false;     // true if this part is removed. The part won't disappear until the end of the turn
                                  // so our indices can remain consistent.

    /** Is part currently active/ready for use? */
    bool enabled = false;

    int flags        = 0;         //
    int passenger_id = 0;         // carrying passenger

    bool open = false;            // door is open
    int direction = 0;            // direction the part is facing

    // Coordinates for some kind of target; jumper cables and turrets use this
    // Two coord pairs are stored: actual target point, and target vehicle center.
    // Both cases use absolute coordinates (relative to world origin)
    std::pair<tripoint, tripoint> target;

private:
    /** What type of part is this? */
    vpart_id id;

    /** As a performance optimisation we cache the part information here on first lookup */
    mutable const vpart_info *info_cache = nullptr;

    item base;
    std::list<item> items; // inventory

    /** Preferred ammo type when multiple are available */
    itype_id ammo_pref = "null";

    /**
     *  What NPC (if any) is assigned to this part (seat, turret etc)?
     *  @see vehicle_part::crew() accessor which excludes dead and non-allied NPC's
     */
    int crew_id = -1;

public:
    /** Get part definition common to all parts of this type */
    const vpart_info &info() const;

    // json saving/loading
    using JsonSerializer::serialize;
    void serialize(JsonOut &jsout) const override;
    using JsonDeserializer::deserialize;
    void deserialize(JsonIn &jsin) override;

    /**
     * Generate the corresponding item from this vehicle part. It includes
     * the hp (item damage), fuel charges (battery or liquids), aspect, ...
     */
    item properties_to_item() const;
};

class turret_data {
    friend vehicle;

    public:
        turret_data() = default;
        turret_data( const turret_data & ) = delete;
        turret_data &operator=( const turret_data & ) = delete;
        turret_data( turret_data && ) = default;
        turret_data &operator=( turret_data && ) = default;

        /** Is this a valid instance? */
        explicit operator bool() const {
            return veh && part;
        }

        std::string name() const;

        /** Get base item location */
        item_location base();
        const item_location base() const;

        /** Quantity of ammunition available for use */
        long ammo_remaining() const;

        /** Maximum quantity of ammunition turret can itself contain */
        long ammo_capacity() const;

        /** Specific ammo data or returns nullptr if no ammo available */
        const itype *ammo_data() const;

        /** Specific ammo type or returns "null" if no ammo available */
        itype_id ammo_current() const;

        /** What ammo is available for this turret (may be multiple if uses tanks) */
        std::set<itype_id> ammo_options() const;

        /** Attempts selecting ammo type and returns true if selection was valid */
        bool ammo_select( const itype_id &ammo );

        /** Effects inclusive of any from ammo loaded from tanks */
        std::set<std::string> ammo_effects() const;

        /** Maximum range considering current ammo (if any) */
        int range() const;

        /** Fire at @ref target returning number of shots (may be zero) */
        int fire( player &p, const tripoint &target );

        bool can_reload() const;
        bool can_unload() const;

        enum class status {
            invalid,
            no_ammo,
            no_power,
            ready
        };

        status query() const;

    private:
        turret_data( vehicle *veh, vehicle_part *part )
            : veh( veh ), part( part ) {}

    protected:
        vehicle *veh = nullptr;
        vehicle_part *part = nullptr;
};

/**
 * Struct used for storing labels
 * (easier to json opposed to a std::map<point, std::string>)
 */
struct label : public JsonSerializer, public JsonDeserializer {
    label() = default;
    label(int const x, int const y) : x(x), y(y) {}
    label(const int x, const int y, std::string text) : x(x), y(y), text(std::move(text)) {}

    int         x = 0;
    int         y = 0;
    std::string text;

    // these are stored in a set
    bool operator<(const label &rhs) const noexcept {
        return (x != rhs.x) ? (x < rhs.x) : (y < rhs.y);
    }

    // json saving/loading
    using JsonSerializer::serialize;
    void serialize(JsonOut &jsout) const override;
    using JsonDeserializer::deserialize;
    void deserialize(JsonIn &jsin) override;
};

/**
 * A vehicle as a whole with all its components.
 *
 * This object can occupy multiple tiles, the objects actually visible
 * on the map are of type `vehicle_part`.
 *
 * Facts you need to know about implementation:
 * - Vehicles belong to map. There's `std::vector<vehicle>`
 *   for each submap in grid. When requesting a reference
 *   to vehicle, keep in mind it can be invalidated
 *   by functions such as `map::displace_vehicle()`.
 * - To check if there's any vehicle at given map tile,
 *   call `map::veh_at()`, and check vehicle type (`veh_null`
 *   means there's no vehicle there).
 * - Vehicle consists of parts (represented by vector). Parts have some
 *   constant info: see veh_type.h, `vpart_info` structure and
 *   vpart_list array -- that is accessible through `part_info()` method.
 *   The second part is variable info, see `vehicle_part` structure.
 * - Parts are mounted at some point relative to vehicle position (or starting part)
 *   (`0, 0` in mount coords). There can be more than one part at
 *   given mount coords, and they are mounted in different slots.
 *   Check tileray.h file to see a picture of coordinate axes.
 * - Vehicle can be rotated to arbitrary degree. This means that
 *   mount coords are rotated to match vehicle's face direction before
 *   their actual positions are known. For optimization purposes
 *   mount coords are precalculated for current vehicle face direction
 *   and stored in `precalc[0]`. `precalc[1]` stores mount coords for
 *   next move (vehicle can move and turn). Method `map::displace_vehicle()`
 *   assigns `precalc[1]` to `precalc[0]`. At any time (except
 *   `map::vehmove()` innermost cycle) you can get actual part coords
 *   relative to vehicle's position by reading `precalc[0]`.
 *   Vehicles rotate around a (possibly changing) pivot point, and
 *   the precalc coordinates always put the pivot point at (0,0).
 * - Vehicle keeps track of 3 directions:
 *     Direction | Meaning
 *     --------- | -------
 *     face      | where it's facing currently
 *     move      | where it's moving, it's different from face if it's skidding
 *     turn_dir  | where it will turn at next move, if it won't stop due to collision
 * - Some methods take `part` or `p` parameter. This is the index of a part in
 *   the parts list.
 * - Driver doesn't know what vehicle he drives.
 *   There's only player::in_vehicle flag which
 *   indicates that he is inside vehicle. To figure
 *   out what, you need to ask a map if there's a vehicle
 *   at driver/passenger position.
 * - To keep info consistent, always use
 *   `map::board_vehicle()` and `map::unboard_vehicle()` for
 *   boarding/unboarding player.
 * - To add new predesigned vehicle, add an entry to data/raw/vehicles.json
 *   similar to the existing ones. Keep in mind, that positive x coordinate points
 *   forwards, negative x is back, positive y is to the right, and
 *   negative y to the left:
 *
 *       orthogonal dir left (-Y)
 *            ^
 *       -X ------->  +X (forward)
 *            v
 *       orthogonal dir right (+Y)
 *
 *   When adding parts, function checks possibility to install part at given
 *   coords. If it shows debug messages that it can't add parts, when you start
 *   the game, you did something wrong.
 *   There are a few rules:
 *   1. Every mount point (tile) must begin with a part in the 'structure'
 *      location, usually a frame.
 *   2. No part can stack with itself.
 *   3. No part can stack with another part in the same location, unless that
 *      part is so small as to have no particular location (such as headlights).
 *   If you can't understand why installation fails, try to assemble your
 *   vehicle in game first.
 */
class vehicle : public JsonSerializer, public JsonDeserializer
{
private:
    bool has_structural_part(int dx, int dy) const;
    bool is_structural_part_removed() const;
    void open_or_close(int part_index, bool opening);
    bool is_connected(vehicle_part const &to, vehicle_part const &from, vehicle_part const &excluded) const;
    void add_missing_frames();
    void add_steerable_wheels();

    // direct damage to part (armor protection and internals are not counted)
    // returns damage bypassed
    int damage_direct( int p, int dmg, damage_type type = DT_TRUE );
    // Removes the part, breaks it into pieces and possibly removes parts attached to it
    int break_off( int p, int dmg );
    // Returns if it did actually explode
    bool explode_fuel( int p, damage_type type );
    //damages vehicle controls and security system
    void smash_security_system();
    // get vpart powerinfo for part number, accounting for variable-sized parts and hps.
    int part_power( int index, bool at_full_hp = false ) const;
    int part_power( const vehicle_part &part, bool at_full_hp = false ) const;

    // get vpart epowerinfo for part number.
    int part_epower (int index) const;

    // convert epower (watts) to power.
    static int epower_to_power (int epower);

    // convert power to epower (watts).
    static int power_to_epower (int power);

    //Refresh all caches and re-locate all parts
    void refresh();

    // Do stuff like clean up blood and produce smoke from broken parts. Returns false if nothing needs doing.
    bool do_environmental_effects();

    units::volume total_folded_volume() const;

    /**
     * Find a possibly off-map vehicle. If necessary, loads up its submap through
     * the global MAPBUFFER and pulls it from there. For this reason, you should only
     * give it the coordinates of the origin tile of a target vehicle.
     * @param where Location of the other vehicle's origin tile.
     */
    static vehicle* find_vehicle( const tripoint &where );

    /**
     * Traverses the graph of connected vehicles, starting from start_veh, and continuing
     * along all vehicles connected by some kind of POWER_TRANSFER part.
     * @param start_vehicle The vehicle to start traversing from. NB: the start_vehicle is
     * assumed to have been already visited!
     * @param amount An amount of power to traverse with. This is passed back to the visitor,
     * and reset to the visitor's return value at each step.
     * @param visitor A function(vehicle* veh, int amount, int loss) returning int. The function
     * may do whatever it desires, and may be a lambda (including a capturing lambda).
     * NB: returning 0 from a visitor will stop traversal immediately!
     * @return The last visitor's return value.
     */
    template <typename Func, typename Vehicle>
    static int traverse_vehicle_graph(Vehicle *start_veh, int amount, Func visitor);
public:
    vehicle(const vproto_id &type_id, int veh_init_fuel = -1, int veh_init_status = -1);
    vehicle();
    ~vehicle () override;

    /**
     * Set stat for part constrained by range [0,durability]
     * @note does not invoke base @ref item::on_damage callback
     */
    void set_hp( vehicle_part &pt, int qty );

    /**
     * Apply damage to part constrained by range [0,durability] possibly destroying it
     * @param qty maximum amount by which to adjust damage (negative permissible)
     * @param dmg type of damage which may be passed to base @ref item::on_damage callback
     * @return whether part was destroyed as a result of the damage
     */
    bool mod_hp( vehicle_part &pt, int qty, damage_type dt = DT_NULL );

    // check if given player controls this vehicle
    bool player_in_control(player const &p) const;
    // check if player controls this vehicle remotely
    bool remote_controlled(player const &p) const;

    // init parts state for randomly generated vehicle
    void init_state(int veh_init_fuel, int veh_init_status);

    // damages all parts of a vehicle by a random amount
    void smash();

    // load and init vehicle data from stream. This implies valid save data!
    void load (std::istream &stin);

    // Save vehicle data to stream
    void save (std::ostream &stout);

    using JsonSerializer::serialize;
    void serialize(JsonOut &jsout) const override;
    using JsonDeserializer::deserialize;
    void deserialize(JsonIn &jsin) override;

    /**
     *  Operate vehicle controls
     *  @param pos location of physical controls to operate (ignored during remote operation)
     */
    void use_controls(const tripoint &pos );

    // Fold up the vehicle
    bool fold_up();

    // Attempt to start an engine
    bool start_engine( const int e );

    // Attempt to start the vehicle's active engines
    void start_engines( const bool take_control = false );

    // Engine backfire, making a loud noise
    void backfire( const int e ) const;

    // Honk the vehicle's horn, if there are any
    void honk_horn();
    void beeper_sound();
    void play_music();
    void play_chimes();
    void operate_planter();
    // get vpart type info for part number (part at given vector index)
    const vpart_info& part_info (int index, bool include_removed = false) const;

    // check if certain part can be mounted at certain position (not accounting frame direction)
    bool can_mount (int dx, int dy, const vpart_id &id) const;

    // check if certain part can be unmounted
    bool can_unmount (int p) const;

    // install a new part to vehicle
    int install_part (int dx, int dy, const vpart_id &id, bool force = false );

    // Install a copy of the given part, skips possibility check
    int install_part (int dx, int dy, const vehicle_part &part);

    /** install item @ref obj to vehicle as a vehicle part */
    int install_part( int dx, int dy, const vpart_id& id, item&& obj, bool force = false );

    bool remove_part (int p);
    void part_removal_cleanup ();

    /** Get handle for base item of part */
    item_location part_base( int p );

    /** Get index of part with matching base item or INT_MIN if not found */
    int find_part( const item& it ) const;

    /**
     * Remove a part from a targeted remote vehicle. Useful for, e.g. power cables that have
     * a vehicle part on both sides.
     */
    void remove_remote_part(int part_num);

    std::string const& get_label(int x, int y) const;
    void set_label(int x, int y, std::string text);

    void break_part_into_pieces (int p, int x, int y, bool scatter = false);

    // returns the list of indeces of parts at certain position (not accounting frame direction)
    std::vector<int> parts_at_relative (int dx, int dy, bool use_cache = true) const;

    // returns index of part, inner to given, with certain flag, or -1
    int part_with_feature (int p, const std::string &f, bool unbroken = true) const;
    int part_with_feature_at_relative (const point &pt, const std::string &f, bool unbroken = true) const;
    int part_with_feature (int p, vpart_bitflags f, bool unbroken = true) const;

    /** Check if vehicle has at least one unbroken part matching predicate @ref func */
    bool has_part( const std::function<bool(const vehicle_part &)> &func ) const;

    /**
     *  Check if vehicle has at least one unbroken part with @ref flag
     *  @param enabled if set part must also be enabled to be considered
     */
    bool has_part( const std::string &flag, bool enabled = false ) const;

    /**
     *  Check if vehicle has at least one unbroken part with @ref flag
     *  @param pos limit check for parts to this global position
     *  @param enabled if set part must also be enabled to be considered
     */
    bool has_part( const tripoint &pos, const std::string &flag, bool enabled = false ) const;

    /** Check at least one unbroken part at global position @ref pos matching predicate @ref func */
    bool has_part( const tripoint &pos, const std::function<bool(const vehicle_part &)> &func ) const;

    /**
     *  Get all unbroken vehicle parts with @ref flag
     *  @param enabled if set part must also be enabled to be considered
     */
    std::vector<vehicle_part *> get_parts( const std::string &flag, bool enabled = false );
    std::vector<const vehicle_part *> get_parts( const std::string &flag, bool enabled = false ) const;

    /**
     *  Get all unbroken vehicle parts at @ref pos
     *  @param flag if set only flags with this part will be considered
     *  @param enabled if set part must also be enabled to be considered
     */
    std::vector<vehicle_part *> get_parts( const tripoint &pos, const std::string &flag = "", bool enabled = false );
    std::vector<const vehicle_part *> get_parts( const tripoint &pos, const std::string &flag = "", bool enabled = false ) const;

    /** Test if part can be enabled (unbroken, sufficient fuel etc), optionally displaying failures to user */
    bool can_enable( const vehicle_part &pt, bool alert = false ) const;

    /**
     *  Return the index of the next part to open at `p`'s location
     *
     *  The next part to open is the first unopened part in the reversed list of
     *  parts at part `p`'s coordinates.
     *
     *  @param outside If true, give parts that can be opened from outside only
     *  @return part index or -1 if no part
     */
    int next_part_to_open (int p, bool outside = false) const;

    /**
     *  Return the index of the next part to close at `p`
     *
     *  The next part to open is the first opened part in the list of
     *  parts at part `p`'s coordinates. Returns -1 for no more to close.
     *
     *  @param outside If true, give parts that can be closed from outside only
     *  @return part index or -1 if no part
     */
    int next_part_to_close( int p, bool outside = false ) const;

    // returns indices of all parts in the vehicle with the given flag
    std::vector<int> all_parts_with_feature(const std::string &feature, bool unbroken = true) const;
    std::vector<int> all_parts_with_feature(vpart_bitflags f, bool unbroken = true) const;

    // returns indices of all parts in the given location slot
    std::vector<int> all_parts_at_location(const std::string &location) const;

    // returns true if given flag is present for given part index
    bool part_flag (int p, const std::string &f) const;
    bool part_flag (int p, vpart_bitflags f) const;

    // Returns the obstacle that shares location with this part (useful in some map code)
    // Open doors don't count as obstacles, but closed do
    // Broken parts are also never obstacles
    int obstacle_at_part( int p ) const;

    // Translate mount coords "p" using current pivot direction and anchor and return tile coords
    point coord_translate (const point &p) const;

    // Translate mount coords "p" into tile coords "q" using given pivot direction and anchor
    void coord_translate (int dir, const point &pivot, const point &p, point &q) const;

    // Seek a vehicle part which obstructs tile with given coords relative to vehicle position
    int part_at( int dx, int dy ) const;
    int global_part_at( int x, int y ) const;
    int global_part_at( const tripoint &p ) const;
    int part_displayed_at( int local_x, int local_y ) const;
    int roof_at_part( int p ) const;

    // Given a part, finds its index in the vehicle
    int index_of_part(const vehicle_part *part, bool check_removed = false) const;

    // get symbol for map
    char part_sym( int p, bool exact = false ) const;
    const vpart_id &part_id_string(int p, char &part_mod) const;

    // get color for map
    nc_color part_color( int p, bool exact = false ) const;

    // Vehicle parts description
    int print_part_desc (WINDOW *win, int y1, int max_y, int width, int p, int hl = -1) const;

    // Get all printable fuel types
    std::vector<itype_id> get_printable_fuel_types() const;

    // Vehicle fuel indicators (all of them)
    void print_fuel_indicators( WINDOW *win, int y, int x, int startIndex = 0, bool fullsize = false,
                                bool verbose = false, bool desc = false, bool isHorizontal = false ) const;

    // Vehicle fuel indicator (by fuel)
    void print_fuel_indicator( void *w, int y, int x, itype_id fuelType,
                               bool verbose = false, bool desc = false ) const;

    // Precalculate mount points for (idir=0) - current direction or (idir=1) - next turn direction
    void precalc_mounts (int idir, int dir, const point &pivot);

    // get a list of part indeces where is a passenger inside
    std::vector<int> boarded_parts() const;

    // get passenger at part p
    player *get_passenger (int p) const;

    /**
     * Get the coordinates (in map squares) of this vehicle, it's the same
     * coordinate system that player::posx uses.
     * Global apparently means relative to the currently loaded map (game::m).
     * This implies:
     * <code>g->m.veh_at(this->global_x(), this->global_y()) == this;</code>
     */
    int global_x() const;
    int global_y() const;
    point global_pos() const;
    tripoint global_pos3() const;
    /**
     * Get the coordinates of the studied part of the vehicle
     */
    tripoint global_part_pos3( const int &index ) const;
    tripoint global_part_pos3( const vehicle_part &pt ) const;
    /**
     * Really global absolute coordinates in map squares.
     * This includes the overmap, the submap, and the map square.
     */
    point real_global_pos() const;
    tripoint real_global_pos3() const;
    /**
     * All the fuels that are in all the tanks in the vehicle, nicely summed up.
     * Note that empty tanks don't count at all. The value is the amout as it would be
     * reported by @ref fuel_left, it is always greater than 0. The key is the fuel item type.
     */
    std::map<itype_id, long> fuels_left() const;

    /**
     * How much of a given fuel type is available from various sources
     * @param recurse for battery power should networked vehicles be considered?
     * @param reactor for battery power should active reactors be considered?
     */
    int fuel_left( const itype_id &ftype, bool recurse = false, bool reactor = false ) const;

    int fuel_capacity (const itype_id &ftype) const;

    // drains a fuel type (e.g. for the kitchen unit)
    int drain (const itype_id &ftype, int amount);

    /**
     * Get all vehicle lights (excluding any that are destroyed)
     * @param active if true return only lights which are enabled
     */
    std::vector<vehicle_part *> lights( bool active = false );

    /**
     * Try to charge our (and, optionally, connected vehicles') batteries by the given amount.
     * @return amount of charge left over.
     */
    int charge_battery (int amount, bool recurse = true);

    /**
     * Attempt to obtain electrical power from various sources
     * @param amount maximum power (kJ) to obtain
     * @param recurse if insufficient local power available should networked vehicles be drained?
     * @param reactor if insufficient power is available should active reactors will be engaged?
     * @return amount of request unfulfilled (0 if totally successful).
     */
    int discharge( int amount, bool recurse = true, bool reactor = true );

    /**
     * Mark mass caches and pivot cache as dirty
     */
    void invalidate_mass();

    // get the total mass of vehicle, including cargo and passengers
    int total_mass () const;

    // Gets the center of mass calculated for precalc[0] coordinates
    const point &rotated_center_of_mass() const;
    // Gets the center of mass calculated for mount point coordinates
    const point &local_center_of_mass() const;

    // Get the pivot point of vehicle; coordinates are unrotated mount coordinates.
    // This may result in refreshing the pivot point if it is currently stale.
    const point &pivot_point() const;

    // Get the (artificial) displacement of the vehicle due to the pivot point changing
    // between precalc[0] and precalc[1]. This needs to be subtracted from any actual
    // vehicle motion after precalc[1] is prepared.
    point pivot_displacement() const;

    /** Calculate power usage (watts) of all active parts */
    int power_usage() const;

    /** Get currently selected engine (if any) or a null part if none are selected */
    vehicle_part &current_engine();
    const vehicle_part &current_engine() const;

    /** Get current vehicle velocity (m/s) */
    double current_velocity() const;

    /** Get maximum velocity (m/s) when using a specific engine */
    double max_velocity( const vehicle_part &pt ) const;

    /** Highest velocity avoiding engine damage (m/s) when using a specific engine */
    double safe_velocity( const vehicle_part &pt ) const;

    /** Most fuel efficient velocity when using a specific engine */
    double optimal_velocity( const vehicle_part &pt ) const;

    /** Get current gear for specific engine (or zero for engines without discrete gears) */
    int gear( const vehicle_part &pt ) const;

    /** Get current rpm of specific engine (or zero if engine is not running) */
    int rpm( const vehicle_part &pt ) const;

    /** Check if specific engine is currently running above redline? */
    bool overspeed( const vehicle_part &pt ) const;

    /** Get power required (watts) required to replace losses due to friction at current speed  */
    int friction_load() const;

    /** Get acceleration (m/s²) from specific engine dependent upon current @ref load() */
    double acceleration( const vehicle_part &pt ) const;

    // Generate smoke from a part, either at front or back of vehicle depending on velocity.
    void spew_smoke( double joules, int part, int density = 1 );

    /**
     * Generate noise or smoke from a vehicle with a running engine
     * @param load current engine load as proportion of maximum output [0.0-1.0]
     */
    void noise_and_smoke( double load );

    /**
     * Calculates the sum of the area under the wheels of the vehicle.
     * @param boat If true, calculates the area under "wheels" that allow swimming.
     */
    float wheel_area( bool boat ) const;

    /**
     * Physical coefficients used for vehicle calculations.
     * All coefficients have values ranging from 1.0 (ideal) to 0.0 (vehicle can't move).
     */
    /*@{*/
    /**
     * Combined coefficient of aerodynamic and wheel friction resistance of vehicle.
     * Safe velocity and acceleration are multiplied by this value.
     */
    float k_dynamics() const;

    /**
     * Wheel friction coefficient of the vehicle.
     * Inversely proportional to (wheel area + constant).
     * 
     * Affects @ref k_dynamics, which in turn affects velocity and acceleration.
     */
    float k_friction() const;

    /**
     * Air friction coefficient of the vehicle.
     * Affected by vehicle's width and non-passable tiles.
     * Calculated by projecting rays from front of the vehicle to its back.
     * Each ray that contains only passable vehicle tiles causes a small penalty,
     * and each ray that contains an unpassable vehicle tile causes a big penalty.
     *
     * Affects @ref k_dynamics, which in turn affects velocity and acceleration.
     */
    float k_aerodynamics() const;

    /**
     * Mass coefficient of the vehicle.
     * Roughly proportional to vehicle's mass divided by wheel area, times constant.
     * 
     * Affects safe velocity (moderately), acceleration (heavily).
     * Also affects braking (including handbraking) and velocity drop during coasting.
     */
    float k_mass() const;

    /**
     * Traction coefficient of the vehicle.
     * 1.0 on road. Outside roads, depends on mass divided by wheel area
     * and the surface beneath wheels.
     * 
     * Affects safe velocity, acceleration and handling difficulty.
     */
    float k_traction( float wheel_traction_area ) const;
    /*@}*/

    // Extra drag on the vehicle from components other than wheels.
    float drag() const;

    // Calculate if it can move using its wheels or boat parts configuration
    bool sufficient_wheel_config( bool floating ) const;
    bool balanced_wheel_config( bool floating ) const;
    bool valid_wheel_config( bool floating ) const;

    // return the relative effectiveness of the steering (1.0 is normal)
    // <0 means there is no steering installed at all.
    float steering_effectiveness() const;

    /** Returns roughly driving skill level at which there is no chance of fumbling. */
    float handling_difficulty() const;

    // idle fuel consumption
    void idle(bool on_map = true);
    // continuous processing for running vehicle alarms
    void alarm();
    // leak from broken tanks
    void slow_leak();

    // thrust (1) or brake (-1) vehicle
    void thrust (int thd);

    // depending on skid vectors, chance to recover.
    void possibly_recover_from_skid();

    //forward component of velocity.
    float forward_velocity() const;

    // cruise control
    void cruise_thrust (int amount);

    // turn vehicle left (negative) or right (positive), degrees
    void turn (int deg);

    // Returns if any collision occured
    bool collision( std::vector<veh_collision> &colls,
                    const tripoint &dp,
                    bool just_detect, bool bash_floor = false );

    // Handle given part collision with vehicle, monster/NPC/player or terrain obstacle
    // Returns collision, which has type, impulse, part, & target.
    veh_collision part_collision( int part, const tripoint &p,
                                  bool just_detect, bool bash_floor );

    // Process the trap beneath
    void handle_trap( const tripoint &p, int part );

    units::volume max_volume(int part) const; // stub for per-vpart limit
    units::volume free_volume(int part) const;
    units::volume stored_volume(int part) const;
    /**
     * Try to add an item to part's cargo.
     *
     * @ret False if it can't be put here (not a cargo part, adding this would violate
     * the volume limit or item count limit, not all charges can fit, etc.)
     */
    bool add_item( int part, const item &obj );
    /** Like the above */
    bool add_item( vehicle_part &pt, const item &obj );
    /**
     * Add an item counted by charges to the part's cargo.
     *
     * @ret The number of charges added.
     */
    long add_charges( int part, const item &itm );
    /**
     * Position specific item insertion that skips a bunch of safety checks
     * since it should only ever be used by item processing code.
     */
    bool add_item_at( int part, std::list<item>::iterator index, item itm );

    // remove item from part's cargo
    bool remove_item( int part, int itemdex );
    bool remove_item( int part, const item *it );
    std::list<item>::iterator remove_item (int part, std::list<item>::iterator it);

    vehicle_stack get_items( int part ) const;
    vehicle_stack get_items( int part );

    // Generates starting items in the car, should only be called when placed on the map
    void place_spawn_items();

    void gain_moves();

    // reduces velocity to 0
    void stop ();

    void refresh_insides ();

    bool is_inside (int p) const;

    void unboard_all ();

    // Damage individual part. bash means damage
    // must exceed certain threshold to be substracted from hp
    // (a lot light collisions will not destroy parts)
    // Returns damage bypassed
    int damage (int p, int dmg, damage_type type = DT_BASH, bool aimed = true);

    // damage all parts (like shake from strong collision), range from dmg1 to dmg2
    void damage_all (int dmg1, int dmg2, damage_type type, const point &impact);

    //Shifts the coordinates of all parts and moves the vehicle in the opposite direction.
    void shift_parts( point delta );
    bool shift_if_needed();

    void shed_loose_parts();

    /**
     * @name Vehicle turrets
     *
     *@{*/

    /** Get all vehicle turrets (excluding any that are destroyed) */
    std::vector<vehicle_part *> turrets();

    /** Get all vehicle turrets loaded and ready to fire at @ref target */
    std::vector<vehicle_part *> turrets( const tripoint &target );

    /** Get firing data for a turret */
    turret_data turret_query( vehicle_part &pt );
    const turret_data turret_query( const vehicle_part &pt ) const;

    turret_data turret_query( const tripoint &pos );
    const turret_data turret_query( const tripoint &pos ) const;

    /** Set targeting mode for specific turrets */
    void turrets_set_targeting();

    /** Set firing mode for specific turrets */
    void turrets_set_mode();

    /*
     * Set specific target for automatic turret fire
     * @returns whether a valid target was selected
     */
    bool turrets_aim();

    /*@}*/

    /**
     *  Try to assign a crew member (who must be a player ally) to a specific seat
     *  @note enforces NPC's being assigned to only one seat (per-vehicle) at once
     */
    bool assign_seat( vehicle_part &pt, const npc& who );

    // Update the set of occupied points and return a reference to it
    std::set<tripoint> &get_points( bool force_refresh = false );

    // opens/closes doors or multipart doors
    void open(int part_index);
    void close(int part_index);

    // Consists only of parts with the FOLDABLE tag.
    bool is_foldable() const;
    // Restore parts of a folded vehicle.
    bool restore(const std::string &data);
    //handles locked vehicles interaction
    bool interact_vehicle_locked();
    //true if an alarm part is installed on the vehicle
    bool has_security_working() const;
    /**
     *  Opens everything that can be opened on the same tile as `p`
     */
    void open_all_at(int p);

    // upgrades/refilling/etc. see veh_interact.cpp
    void interact ();
    //scoop operation,pickups, battery drain, etc.
    void operate_scoop();
    void operate_reaper();
    void operate_plow();
    //main method for the control of individual engines
    void control_engines();

    /** shows ui menu to select an engine and @return true if change was made */
    bool select_engine();

    //returns whether the engine is enabled or not, and has fueltype
    bool is_engine_type_on(int e, const itype_id &ft) const;
    //returns whether the engine is enabled or not
    bool is_engine_on(int e) const;
    //returns whether the part is enabled or not
    bool is_part_on(int p) const;
    //returns whether the engine uses specified fuel type
    bool is_engine_type(int e, const itype_id &ft) const;

    //mark engine as on or off
    void toggle_specific_engine(int p, bool on);
    void toggle_specific_part(int p, bool on);
    //true if an engine exists with specified type
    //If enabled true, this engine must be enabled to return true
    bool has_engine_type(const itype_id &ft, bool enabled) const;
    //true if an engine exists without the specified type
    //If enabled true, this engine must be enabled to return true
    bool has_engine_type_not(const itype_id &ft, bool enabled) const;
    //prints message relating to vehicle start failure
    void msg_start_engine_fail();
    //remotely open/close doors
    void control_doors();
    // return a vector w/ 'direction' & 'magnitude', in its own sense of the words.
    rl_vec2d velo_vec() const;
    //normalized vectors, from tilerays face & move
    rl_vec2d face_vec() const;
    rl_vec2d move_vec() const;
    // As above, but calculated for the actually used variable `dir`
    rl_vec2d dir_vec() const;
    void on_move();
    /**
     * Update the submap coordinates smx, smy, and update the tracker info in the overmap
     * (if enabled).
     * This should be called only when the vehicle has actually been moved, not when
     * the map is just shifted (in the later case simply set smx/smy directly).
     */
    void set_submap_moved(int x, int y);

    const std::string disp_name() const;

    /** Required strength to be able to successfully lift the vehicle unaided by equipment */
    int lift_strength() const;

    // config values
    std::string name;   // vehicle name
    /**
     * Type of the vehicle as it was spawned. This will never change, but it can be an invalid
     * type (e.g. if the definition of the prototype has been removed from json or if it has been
     * spawned with the default constructor).
     */
    vproto_id type;
    std::vector<vehicle_part> parts;   // Parts which occupy different tiles
    int removed_part_count;            // Subtract from parts.size() to get the real part count.
    std::map<point, std::vector<int> > relative_parts;    // parts_at_relative(x,y) is used alot (to put it mildly)
    std::set<label> labels;            // stores labels
    std::vector<int> alternators;      // List of alternator indices
    std::vector<int> engines;          // List of engine indices
    std::vector<int> reactors;         // List of reactor indices
    std::vector<int> solar_panels;     // List of solar panel indices
    std::vector<int> funnels;          // List of funnel indices
    std::vector<int> loose_parts;      // List of UNMOUNT_ON_MOVE parts
    std::vector<int> wheelcache;       // List of wheels
    std::vector<int> steering;         // List of STEERABLE parts
    std::vector<int> speciality;       // List of parts that will not be on a vehicle very often, or which only one will be present
    std::vector<int> floating;         // List of parts that provide buoyancy to boats
    std::set<std::string> tags;        // Properties of the vehicle

    active_item_cache active_items;

    /**
     * Submap coordinates of the currently loaded submap (see game::m)
     * that contains this vehicle. These values are changed when the map
     * shifts (but the vehicle is not actually moved than, it also stays on
     * the same submap, only the relative coordinates in map::grid have changed).
     * These coordinates must always refer to the submap in map::grid that contains
     * this vehicle.
     * When the vehicle is really moved (by map::displace_vehicle), set_submap_moved
     * is called and updates these values, when the map is only shifted or when a submap
     * is loaded into the map the values are directly set. The vehicles position does
     * not change therefor no call to set_submap_moved is required.
     */
    int smx, smy, smz;

    float alternator_load;

    // Points occupied by the vehicle
    std::set<tripoint> occupied_points;
    calendar occupied_cache_turn = -1; // Turn occupied points were calculated

    // Turn the vehicle was last processed
    calendar last_update_turn = -1;
    // Retroactively pass time spent outside bubble
    // Funnels, solars
    void update_time( const calendar &update_to );

    // save values
    /**
     * Position of the vehicle *inside* the submap that contains the vehicle.
     * This will (nearly) always be in the range (0...SEEX-1).
     * Note that vehicles are "moved" by map::displace_vehicle. You should not
     * set them directly, except when initializing the vehicle or during mapgen.
     */
    int posx = 0;
    int posy = 0;
    tileray face;       // frame direction
    tileray move;       // direction we are moving
    int velocity = 0;       // vehicle current velocity, mph * 100
    int cruise_velocity = 0; // velocity vehicle's cruise control trying to achieve
    int vertical_velocity = 0; // Only used for collisions, vehicle falls instantly
    int om_id;          // id of the om_vehicle struct corresponding to this vehicle
    int turn_dir = 0;       // direction, to which vehicle is turning (player control). will rotate frame on next move

    std::array<point, 2> pivot_anchor; // points used for rotation of mount precalc values
    std::array<int, 2> pivot_rotation = {{ 0, 0 }}; // rotation used for mount precalc values

    int last_turn = 0;      // amount of last turning (for calculate skidding due to handbrake)
    float of_turn;      // goes from ~1 to ~0 while proceeding every turn
    float of_turn_carry;// leftover from prev. turn

    int tracking_epower     = 0; // total power consumed by tracking devices (why would you use more than one?)
    int alarm_epower        = 0;
    int camera_epower       = 0; // power consumed by camera system
    int extra_drag          = 0;
    // TODO: change these to a bitset + enum?
    bool engine_on                  = false; // at least one engine is on, of any type
    bool tracking_on                = false; // vehicle tracking on/off
    bool is_locked                  = false; // vehicle has no key
    bool is_alarm_on                = false; // vehicle has alarm on
    bool camera_on                  = false;
    bool skidding                   = false; // skidding mode
    bool check_environmental_effects= false; // has bloody or smoking parts
    bool insides_dirty              = true;  // "inside" flags are outdated and need refreshing
    bool falling                    = false; // Is the vehicle hanging in the air and expected to fall down in the next turn?

private:
    void refresh_pivot() const;                // refresh pivot_cache, clear pivot_dirty

    mutable bool pivot_dirty;                  // if true, pivot_cache needs to be recalculated
    mutable point pivot_cache;                 // cached pivot point

    void refresh_mass() const;
    void calc_mass_center( bool precalc ) const;

    /** empty the contents of a tank, battery or turret spilling liquids randomly on the ground */
    void leak_fuel( vehicle_part &pt );

    /*
     * Fire turret at automatically acquired targets
     * @return number of shots actually fired (which may be zero)
     */
    int automatic_fire_turret( vehicle_part &pt );

    mutable bool mass_dirty                     = true;
    mutable bool mass_center_precalc_dirty      = true;
    mutable bool mass_center_no_precalc_dirty   = true;

    mutable int mass_cache;
    mutable point mass_center_precalc;
    mutable point mass_center_no_precalc;
};

#endif
