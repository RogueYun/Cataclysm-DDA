#include "cata_utility.h"

#include "options.h"
#include "material.h"
#include "enums.h"
#include "item.h"
#include "creature.h"
#include "translations.h"
#include "debug.h"
#include "mapsharing.h"
#include "output.h"
#include "json.h"
#include "filesystem.h"
#include "item_search.h"

#include <algorithm>
#include <cmath>

double round_up( double val, unsigned int dp )
{
    const double denominator = std::pow( 10.0, double( dp ) );
    return std::ceil( denominator * val ) / denominator;
}

bool isBetween( int test, int down, int up )
{
    return test > down && test < up;
}

bool lcmatch( const std::string &str, const std::string &qry )
{
    std::string needle;
    needle.reserve( qry.size() );
    std::transform( qry.begin(), qry.end(), std::back_inserter( needle ), tolower );

    std::string haystack;
    haystack.reserve( str.size() );
    std::transform( str.begin(), str.end(), std::back_inserter( haystack ), tolower );

    return haystack.find( needle ) != std::string::npos;
}

std::vector<map_item_stack> filter_item_stacks( std::vector<map_item_stack> stack,
        std::string filter )
{
    std::vector<map_item_stack> ret;

    std::string sFilterTemp = filter;
    auto z = item_filter_from_string( filter );
    std::copy_if( stack.begin(),
                  stack.end(),
                  std::back_inserter( ret ),
    [z]( const map_item_stack & a ) {
        if( a.example != nullptr ) {
            return z( *a.example );
        }
        return false;
    }
                );

    return ret;
}

//returns the first non priority items.
int list_filter_high_priority( std::vector<map_item_stack> &stack, std::string priorities )
{
    //TODO:optimize if necessary
    std::vector<map_item_stack> tempstack; // temp
    const auto filter_fn = item_filter_from_string( priorities );
    for( auto it = stack.begin(); it != stack.end(); ) {
        if( priorities.empty() || ( it->example != nullptr && !filter_fn( *it->example ) ) ) {
            tempstack.push_back( *it );
            it = stack.erase( it );
        } else {
            it++;
        }
    }

    int id = stack.size();
    for( auto &elem : tempstack ) {
        stack.push_back( elem );
    }
    return id;
}

int list_filter_low_priority( std::vector<map_item_stack> &stack, int start,
                              std::string priorities )
{
    //TODO:optimize if necessary
    std::vector<map_item_stack> tempstack; // temp
    const auto filter_fn = item_filter_from_string( priorities );
    for( auto it = stack.begin() + start; it != stack.end(); ) {
        if( !priorities.empty() && it->example != nullptr && filter_fn( *it->example ) ) {
            tempstack.push_back( *it );
            it = stack.erase( it );
        } else {
            it++;
        }
    }

    int id = stack.size();
    for( auto &elem : tempstack ) {
        stack.push_back( elem );
    }
    return id;
}

// Operator overload required by sort interface.
bool pair_greater_cmp::operator()( const std::pair<int, tripoint> &a,
                                   const std::pair<int, tripoint> &b )
{
    return a.first > b.first;
}

// --- Library functions ---
// This stuff could be moved elsewhere, but there
// doesn't seem to be a good place to put it right now.

// Basic logistic function.
double logarithmic( double t )
{
    return 1 / ( 1 + exp( -t ) );
}

// Logistic curve [-6,6], flipped and scaled to
// range from 1 to 0 as pos goes from min to max.
double logarithmic_range( int min, int max, int pos )
{
    const double LOGI_CUTOFF = 4;
    const double LOGI_MIN = logarithmic( -LOGI_CUTOFF );
    const double LOGI_MAX = logarithmic( +LOGI_CUTOFF );
    const double LOGI_RANGE = LOGI_MAX - LOGI_MIN;

    if( min >= max ) {
        debugmsg( "Invalid interval (%d, %d).", min, max );
        return 0.0;
    }

    // Anything beyond (min,max) gets clamped.
    if( pos <= min ) {
        return 1.0;
    } else if( pos >= max ) {
        return 0.0;
    }

    // Normalize the pos to [0,1]
    double range = max - min;
    double unit_pos = ( pos - min ) / range;

    // Scale and flip it to [+LOGI_CUTOFF,-LOGI_CUTOFF]
    double scaled_pos = LOGI_CUTOFF - 2 * LOGI_CUTOFF * unit_pos;

    // Get the raw logistic value.
    double raw_logistic = logarithmic( scaled_pos );

    // Scale the output to [0,1]
    return ( raw_logistic - LOGI_MIN ) / LOGI_RANGE;
}

int bound_mod_to_vals( int val, int mod, int max, int min )
{
    if( val + mod > max && max != 0 ) {
        mod = std::max( max - val, 0 );
    }
    if( val + mod < min && min != 0 ) {
        mod = std::min( min - val, 0 );
    }
    return mod;
}

const char *velocity_units( const units_type vel_units )
{
    if( get_option<std::string>( "USE_METRIC_SPEEDS" ) == "mph" ) {
        return _( "mph" );
    } else {
        switch( vel_units ) {
            case VU_VEHICLE:
                return _( "km/h" );
            case VU_WIND:
                return _( "m/s" );
        }
    }
    return "error: unknown units!";
}

const char *weight_units()
{
    return get_option<std::string>( "USE_METRIC_WEIGHTS" ) == "lbs" ? _( "lbs" ) : _( "kg" );
}

const char *volume_units_abbr()
{
    const std::string vol_units = get_option<std::string>( "VOLUME_UNITS" );
    if( vol_units == "c" ) {
        return _( "c" );
    } else if( vol_units == "l" ) {
        return _( "L" );
    } else {
        return _( "qt" );
    }
}

const char *volume_units_long()
{
    const std::string vol_units = get_option<std::string>( "VOLUME_UNITS" );
    if( vol_units == "c" ) {
        return _( "cup" );
    } else if( vol_units == "l" ) {
        return _( "liter" );
    } else {
        return _( "quart" );
    }
}

/**
* Convert internal velocity units to units defined by user
*/
double convert_velocity( int velocity, const units_type vel_units )
{
    // internal units to mph conversion
    double ret = double( velocity ) / 100;

    if( get_option<std::string>( "USE_METRIC_SPEEDS" ) == "km/h" ) {
        switch( vel_units ) {
            case VU_VEHICLE:
                // mph to km/h conversion
                ret *= 1.609f;
                break;
            case VU_WIND:
                // mph to m/s conversion
                ret *= 0.447f;
                break;
        }
    }
    return ret;
}

/**
* Convert weight in grams to units defined by user (kg or lbs)
*/
double convert_weight( int weight )
{
    double ret;
    ret = double( weight );
    if( get_option<std::string>( "USE_METRIC_WEIGHTS" ) == "kg" ) {
        ret /= 1000;
    } else {
        ret /= 453.6;
    }
    return ret;
}

/**
* Convert volume from ml to units defined by user.
*/
double convert_volume( int volume )
{
    return convert_volume( volume, NULL );
}

/**
* Convert volume from ml to units defined by user,
* optionally returning the units preferred scale.
*/
double convert_volume( int volume, int *out_scale )
{
    double ret = volume;
    int scale = 0;
    const std::string vol_units = get_option<std::string>( "VOLUME_UNITS" );
    if( vol_units == "c" ) {
        ret *= 0.004;
        scale = 1;
    } else if( vol_units == "l" ) {
        ret *= 0.001;
        scale = 2;
    } else {
        ret *= 0.00105669;
        scale = 2;
    }
    if( out_scale != NULL ) {
        *out_scale = scale;
    }
    return ret;
}

double temp_to_celsius( double fahrenheit )
{
    return ( ( fahrenheit - 32.0 ) * 5.0 / 9.0 );
}

double clamp_to_width( double value, int width, int &scale )
{
    return clamp_to_width( value, width, scale, NULL );
}

/**
* Clamp (number and space wise) value to with,
* taking into account the specified preferred scale,
* returning the adjusted (shortened) scale that best fit the width,
* optionally returning a flag that indicate if the value was truncated to fit the width
*/
double clamp_to_width( double value, int width, int &scale, bool *out_truncated )
{
    if( out_truncated != NULL ) {
        *out_truncated = false;
    }
    if( value >= std::pow( 10.0, width ) ) {
        // above the maximum number we can fit in the width without decimal
        // show the bigest number we can without decimal
        // flag as truncated
        value = std::pow( 10.0, width ) - 1.0;
        scale = 0;
        if( out_truncated != NULL ) {
            *out_truncated = true;
        }
    } else if( scale > 0 ) {
        for( int s = 1; s <= scale; s++ ) {
            int scale_width = 1 + s; // 1 decimal separator + "s"
            if( width > scale_width && value >= std::pow( 10.0, width - scale_width ) ) {
                // above the maximum number we can fit in the width with "s" decimals
                // show this number with one less decimal than "s"
                scale = s - 1;
                break;
            }
        }
    }
    return value;
}

float multi_lerp( const std::vector<std::pair<float, float>> &points, float x )
{
    size_t i = 0;
    while( i < points.size() && points[i].first <= x ) {
        i++;
    }

    if( i == 0 ) {
        return points.front().second;
    } else if( i >= points.size() ) {
        return points.back().second;
    }

    // How far are we along the way from last threshold to current one
    const float t = ( x - points[i - 1].first ) /
                    ( points[i].first - points[i - 1].first );

    // Linear interpolation of values at relevant thresholds
    return ( t * points[i].second ) + ( ( 1 - t ) * points[i - 1].second );
}

ofstream_wrapper::ofstream_wrapper( const std::string &path )
{
    file_stream.open( path.c_str(), std::ios::binary );
    if( !file_stream.is_open() ) {
        throw std::runtime_error( "opening file failed" );
    }
}

ofstream_wrapper::~ofstream_wrapper() = default;

void ofstream_wrapper::close()
{
    file_stream.close();
    if( file_stream.fail() ) {
        throw std::runtime_error( "writing to file failed" );
    }
}

bool write_to_file( const std::string &path, const std::function<void( std::ostream & )> &writer,
                    const char *const fail_message )
{
    try {
        ofstream_wrapper fout( path );
        writer( fout.stream() );
        fout.close();
        return true;

    } catch( const std::exception &err ) {
        if( fail_message ) {
            popup( _( "Failed to write %1$s to \"%2$s\": %3$s" ), fail_message, path.c_str(), err.what() );
        }
        return false;
    }
}

ofstream_wrapper_exclusive::ofstream_wrapper_exclusive( const std::string &path )
    : path( path )
{
    fopen_exclusive( file_stream, path.c_str(), std::ios::binary );
    if( !file_stream.is_open() ) {
        throw std::runtime_error( _( "opening file failed" ) );
    }
}

ofstream_wrapper_exclusive::~ofstream_wrapper_exclusive()
{
    if( file_stream.is_open() ) {
        fclose_exclusive( file_stream, path.c_str() );
    }
}

void ofstream_wrapper_exclusive::close()
{
    fclose_exclusive( file_stream, path.c_str() );
    if( file_stream.fail() ) {
        throw std::runtime_error( _( "writing to file failed" ) );
    }
}

bool write_to_file_exclusive( const std::string &path,
                              const std::function<void( std::ostream & )> &writer, const char *const fail_message )
{
    try {
        ofstream_wrapper_exclusive fout( path );
        writer( fout.stream() );
        fout.close();
        return true;

    } catch( const std::exception &err ) {
        if( fail_message ) {
            popup( _( "Failed to write %1$s to \"%2$s\": %3$s" ), fail_message, path.c_str(), err.what() );
        }
        return false;
    }
}

bool read_from_file( const std::string &path, const std::function<void( std::istream & )> &reader )
{
    try {
        std::ifstream fin( path, std::ios::binary );
        if( !fin ) {
            throw std::runtime_error( "opening file failed" );
        }
        reader( fin );
        if( fin.bad() ) {
            throw std::runtime_error( "reading file failed" );
        }
        return true;

    } catch( const std::exception &err ) {
        debugmsg( _( "Failed to read from \"%1$s\": %2$s" ), path.c_str(), err.what() );
        return false;
    }
}

bool read_from_file( const std::string &path, const std::function<void( JsonIn & )> &reader )
{
    return read_from_file( path, [&reader]( std::istream & fin ) {
        JsonIn jsin( fin );
        reader( jsin );
    } );
}

bool read_from_file( const std::string &path, JsonDeserializer &reader )
{
    return read_from_file( path, [&reader]( JsonIn & jsin ) {
        reader.deserialize( jsin );
    } );
}

bool read_from_file_optional( const std::string &path,
                              const std::function<void( std::istream & )> &reader )
{
    // Note: slight race condition here, but we'll ignore it. Worst case: the file
    // exists and got removed before reading it -> reading fails with a message
    // Or file does not exists, than everything works fine because it's optional anyway.
    return file_exist( path ) && read_from_file( path, reader );
}

bool read_from_file_optional( const std::string &path,
                              const std::function<void( JsonIn & )> &reader )
{
    return read_from_file_optional( path, [&reader]( std::istream & fin ) {
        JsonIn jsin( fin );
        reader( jsin );
    } );
}

bool read_from_file_optional( const std::string &path, JsonDeserializer &reader )
{
    return read_from_file_optional( path, [&reader]( JsonIn & jsin ) {
        reader.deserialize( jsin );
    } );
}
