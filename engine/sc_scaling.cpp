// ==========================================================================
// SimulationCraft for Star Wars: The Old Republic
// http://code.google.com/p/simulationcraft-swtor/
// ==========================================================================

#include "simulationcraft.hpp"

namespace { // ANONYMOUS NAMESPACE ==========================================

// is_scaling_stat ==========================================================

static bool is_scaling_stat( sim_t* sim,
                             int    stat )
{
  if ( ! sim -> scaling -> scale_only_str.empty() )
  {
    std::vector<std::string> stat_list;
    int num_stats = util_t::string_split( stat_list, sim -> scaling -> scale_only_str, ",:;/|" );
    bool found = false;
    for ( int i=0; i < num_stats && ! found; i++ )
    {
      found = ( util_t::parse_stat_type( stat_list[ i ] ) == stat );
    }
    if ( ! found ) return false;
  }

  for ( player_t* p = sim -> player_list; p; p = p -> next )
  {
    if ( p -> quiet ) continue;
    if ( p -> is_pet() ) continue;

    if ( p -> scales_with[ stat ] ) return true;
  }

  return false;
}

// stat_may_cap =============================================================

static bool stat_may_cap( int stat )
{
  if ( stat == STAT_ACCURACY_RATING ) return true;
  return false;
}

// parse_normalize_scale_factors ============================================

static bool parse_normalize_scale_factors( sim_t* sim,
                                           const std::string& name,
                                           const std::string& value )
{
  if ( name != "normalize_scale_factors" ) return false;

  if ( value == "1" )
  {
    sim -> scaling -> normalize_scale_factors = 1;
  }
  else
  {
    if ( ( sim -> normalized_stat = util_t::parse_stat_type( value ) ) == STAT_NONE )
    {
      return false;
    }

    sim -> scaling -> normalize_scale_factors = 1;
  }

  return true;
}

// scaling_t::compare_scale_factors =========================================

struct compare_scale_factors
{
  const player_t* player;
  compare_scale_factors( const player_t* p ) : player( p ) {}
  bool operator()( stat_type l, stat_type r ) const
  {
    return( player -> scaling.get_stat( l ) >
            player -> scaling.get_stat( r ) );
  }
};

} // ANONYMOUS NAMESPACE ====================================================

// ==========================================================================
// Scaling
// ==========================================================================

// scaling_t::scaling_t =====================================================

scaling_t::scaling_t( sim_t* s ) :
  sim( s ), baseline_sim( 0 ), ref_sim( 0 ), delta_sim( 0 ), ref_sim2( 0 ), delta_sim2( 0 ),
  scale_stat( STAT_NONE ),
  scale_value( 0 ),
  scale_delta_multiplier( 1.0 ),
  calculate_scale_factors( 0 ),
  center_scale_delta( 0 ),
  positive_scale_delta( 0 ),
  scale_lag( 0 ),
  scale_factor_noise( 0.10 ),
  normalize_scale_factors( 0 ),
  smooth_scale_factors( 0 ),
  debug_scale_factors( 0 ),
  current_scaling_stat( STAT_NONE ),
  num_scaling_stats( 0 ),
  remaining_scaling_stats( 0 ),
  scale_alacrity_iterations( 1.0 ),
  scale_expertise_iterations( 1.0 ),
  scale_crit_iterations( 1.0 ),
  scale_accuracy_iterations( 1.0 ),
  scale_over( "" ), scale_over_player( "" )
{
  create_options();
}

// scaling_t::progress ======================================================

double scaling_t::progress( std::string& phase )
{
  if ( ! calculate_scale_factors ) return 1.0;

  if ( num_scaling_stats <= 0 ) return 0.0;

  if ( current_scaling_stat <= 0 )
  {
    phase = "Baseline";
    if ( ! baseline_sim ) return 0;
    return baseline_sim -> current_iteration / ( double ) sim -> iterations;
  }

  phase  = "Scaling - ";
  phase += util_t::stat_type_abbrev( current_scaling_stat );

  double stat_progress = ( num_scaling_stats - remaining_scaling_stats ) / ( double ) num_scaling_stats;

  double divisor = num_scaling_stats * 2.0;

  if ( ref_sim  ) stat_progress += ref_sim  -> current_iteration / ( ref_sim  -> iterations * divisor );
  if ( ref_sim2 ) stat_progress += ref_sim2 -> current_iteration / ( ref_sim2 -> iterations * divisor );

  if ( delta_sim  ) stat_progress += delta_sim  -> current_iteration / ( delta_sim  -> iterations * divisor );
  if ( delta_sim2 ) stat_progress += delta_sim2 -> current_iteration / ( delta_sim2 -> iterations * divisor );

  return stat_progress;
}

// scaling_t::init_deltas ===================================================

void scaling_t::init_deltas()
{
  assert ( scale_delta_multiplier != 0 );

  for ( attribute_type i = ATTRIBUTE_NONE; ++i < ATTRIBUTE_MAX; )
  {
    if ( stats.attribute[ i ] == 0 ) stats.attribute[ i ] = scale_delta_multiplier * ( smooth_scale_factors ? 15 : 30 );
  }

  if ( stats.power == 0 ) stats.power = scale_delta_multiplier * ( smooth_scale_factors ? 15 : 30 );
  if ( stats.force_power == 0 ) stats.force_power = scale_delta_multiplier * ( smooth_scale_factors ? 15 : 30 );
  if ( stats.tech_power == 0 )  stats.tech_power  = scale_delta_multiplier * ( smooth_scale_factors ? 15 : 30 );

  if ( stats.surge_rating == 0 ) stats.surge_rating = scale_delta_multiplier * ( smooth_scale_factors ? 15 : 30 );


  if ( stats.expertise_rating == 0 )
  {
    stats.expertise_rating =  scale_delta_multiplier * ( smooth_scale_factors ? -15 :  -30 );
    if ( positive_scale_delta ) stats.expertise_rating *= -1;
  }

  if ( stats.accuracy_rating == 0 )
  {
    stats.accuracy_rating = scale_delta_multiplier * ( smooth_scale_factors ? -15 : -30 );
    if ( positive_scale_delta ) stats.accuracy_rating *= -1;
  }
  if ( stats.accuracy_rating2 == 0 )
  {
    stats.accuracy_rating2 = scale_delta_multiplier * ( smooth_scale_factors ? 15 : 30 );
    if ( positive_scale_delta ) stats.accuracy_rating2 *= -1;
  }

  if ( stats.crit_rating  == 0    ) stats.crit_rating     = scale_delta_multiplier * ( smooth_scale_factors ?  15 :  30 );
  if ( stats.alacrity_rating == 0 ) stats.alacrity_rating = scale_delta_multiplier * ( smooth_scale_factors ?  15 :  30 );

  // Defensive
  if ( stats.defense_rating == 0 ) stats.defense_rating = scale_delta_multiplier * ( smooth_scale_factors ? 15 :  30 );
  if ( stats.shield_rating == 0 ) stats.shield_rating = scale_delta_multiplier * ( smooth_scale_factors ? 15 :  30 );
  if ( stats.absorb_rating == 0 ) stats.absorb_rating = scale_delta_multiplier * ( smooth_scale_factors ? 15 :  30 );
  if ( stats.armor == 0 ) stats.armor = smooth_scale_factors ? 150 : 300;


  if ( stats.weapon_dmg            == 0 ) stats.weapon_dmg            = scale_delta_multiplier * ( smooth_scale_factors ? 15 : 30 );
  if ( stats.weapon_offhand_dmg    == 0 ) stats.weapon_offhand_dmg    = scale_delta_multiplier * ( smooth_scale_factors ? 15 : 30 );

}

// scaling_t::analyze_stats =================================================

void scaling_t::analyze_stats()
{
  if ( ! calculate_scale_factors ) return;

  int num_players = ( int ) sim -> players_by_name.size();
  if ( num_players == 0 ) return;

  remaining_scaling_stats = 0;
  for ( stat_type i = STAT_NONE; i < STAT_MAX; i++ )
    if ( is_scaling_stat( sim, i ) && ( stats.get_stat( i ) != 0 ) )
      remaining_scaling_stats++;
  num_scaling_stats = remaining_scaling_stats;

  if ( num_scaling_stats == 0 ) return;

  baseline_sim = sim;
  if ( smooth_scale_factors )
  {
    if ( sim -> report_progress )
    {
      util_t::fprintf( stdout, "\nGenerating smooth baseline...\n" );
      fflush( stdout );
    }

    baseline_sim = new sim_t( sim );
    baseline_sim -> scaling -> scale_stat = STAT_MAX;
    --baseline_sim -> scaling -> scale_stat;
    baseline_sim -> execute();
  }

  for ( stat_type i = STAT_NONE; i < STAT_MAX; i++ )
  {
    if ( sim -> canceled ) break;

    if ( ! is_scaling_stat( sim, i ) ) continue;

    double scale_delta = stats.get_stat( i );
    if ( scale_delta == 0.0 ) continue;

    current_scaling_stat = i;

    if ( sim -> report_progress )
    {
      util_t::fprintf( stdout, "\nGenerating scale factors for %s...\n", util_t::stat_type_string( i ) );
      fflush( stdout );
    }

    bool center = center_scale_delta && ! stat_may_cap( i );

    ref_sim = center ? new sim_t( sim ) : baseline_sim;

    delta_sim = new sim_t( sim );
    delta_sim -> scaling -> scale_stat = i;
    delta_sim -> scaling -> scale_value = +scale_delta / ( center ? 2 : 1 );
    if ( i == STAT_ALACRITY_RATING && ( scale_alacrity_iterations != 0 ) ) delta_sim -> iterations = ( int ) ( delta_sim -> iterations * scale_alacrity_iterations );
    if ( i == STAT_EXPERTISE_RATING && ( scale_expertise_iterations != 0 ) ) delta_sim -> iterations = ( int ) ( delta_sim -> iterations * scale_expertise_iterations );
    if ( i == STAT_CRIT_RATING && ( scale_crit_iterations != 0 ) ) delta_sim -> iterations = ( int ) ( delta_sim -> iterations * scale_crit_iterations );
    if ( i == STAT_ACCURACY_RATING && ( scale_accuracy_iterations != 0 ) ) delta_sim -> iterations = ( int ) ( delta_sim -> iterations * scale_accuracy_iterations );
    delta_sim -> execute();

    if ( center )
    {
      ref_sim -> scaling -> scale_stat = i;
      ref_sim -> scaling -> scale_value = center ? -( scale_delta / 2 ) : 0;
      if ( i == STAT_ALACRITY_RATING && ( scale_alacrity_iterations != 0 ) ) ref_sim -> iterations = ( int ) ( ref_sim -> iterations * scale_alacrity_iterations );
      if ( i == STAT_EXPERTISE_RATING && ( scale_expertise_iterations != 0 ) ) ref_sim -> iterations = ( int ) ( ref_sim -> iterations * scale_expertise_iterations );
      if ( i == STAT_CRIT_RATING && ( scale_crit_iterations != 0 ) ) ref_sim -> iterations = ( int ) ( ref_sim -> iterations * scale_crit_iterations );
      if ( i == STAT_ACCURACY_RATING && ( scale_accuracy_iterations != 0 ) ) ref_sim -> iterations = ( int ) ( ref_sim -> iterations * scale_accuracy_iterations );
      ref_sim -> execute();
    }

    for ( int j=0; j < num_players; j++ )
    {
      player_t* p = sim -> players_by_name[ j ];

      if ( ! p -> scales_with[ i ] ) continue;

      player_t*   ref_p =   ref_sim -> find_player( p -> name() );
      player_t* delta_p = delta_sim -> find_player( p -> name() );



      double divisor = scale_delta;

      if ( delta_p -> invert_scaling )
        divisor = -divisor;

      if ( divisor < 0.0 ) divisor += ref_p -> over_cap[ i ];

      double delta_score = scale_over_function( delta_sim, delta_p );
      double   ref_score = scale_over_function(   ref_sim,   ref_p );

      double delta_error = scale_over_function_error( delta_sim, delta_p ) * delta_sim -> confidence_estimator;
      double   ref_error = scale_over_function_error(   ref_sim,   ref_p ) * ref_sim -> confidence_estimator;

      p -> scaling_delta_dps.set_stat( i, delta_score );

      double score = ( delta_score - ref_score ) / divisor;
      double error = sqrt ( delta_error * delta_error + ref_error * ref_error );

      if ( scale_factor_noise > 0 &&
           scale_factor_noise < error / fabs( delta_score - ref_score ) )
      {
        sim -> errorf( "Player %s may have insufficient iterations (%d) to calculate scale factor for %s (error is >%.0f%% delta score)\n",
                       p -> name(), sim -> iterations, util_t::stat_type_string( i ), scale_factor_noise * 100.0 );
      }

      error = fabs( error / divisor );

      if ( fabs( divisor ) < 1.0 ) // For things like Weapon Speed, show the gain per 0.1 speed gain rather than every 1.0.
      {
        score /= 10.0;
        error /= 10.0;
        delta_error /= 10.0;
      }

      analyze_ability_stats( i, divisor, p, ref_p, delta_p );

      if ( center )
        p -> scaling_compare_error.set_stat( i, error );
      else
        p -> scaling_compare_error.set_stat( i, delta_error / divisor );


      p -> scaling.set_stat( i, score );
      p -> scaling_error.set_stat( i, error );
    }

    if ( debug_scale_factors )
    {
      log_t::output( ref_sim, "\nref_sim report for %s...\n", util_t::stat_type_string( i ) );
      report_t::print_text( sim -> output_file,   ref_sim, true );
      log_t::output( delta_sim, "\ndelta_sim report for %s...\n", util_t::stat_type_string( i ) );
      report_t::print_text( sim -> output_file, delta_sim, true );
    }

    if ( ref_sim != baseline_sim && ref_sim != sim )
    {
      delete ref_sim;
      ref_sim = 0;
    }
    delete delta_sim;  delta_sim  = 0;

    remaining_scaling_stats--;
  }

  if ( baseline_sim != sim ) delete baseline_sim;
  baseline_sim = 0;
}

// scaling_t::analyze_ability_stats ===================================================

void scaling_t::analyze_ability_stats( stat_type stat, double delta, player_t* p, player_t* ref_p, player_t* delta_p )
{
  if ( p -> sim -> statistics_level < 3 )
    return;

  for ( stats_t* s = p -> stats_list; s; s = s -> next )
  {
    stats_t* ref_s = ref_p -> find_stats( s -> name_str );
    stats_t* delta_s = delta_p -> find_stats( s -> name_str );
    assert( ref_s && delta_s );
    double score = ( delta_s -> portion_aps.mean - ref_s -> portion_aps.mean ) / delta;
    s -> scaling.set_stat( stat, score );
    double x = p -> sim -> confidence_estimator;
    double error = fabs( sqrt ( delta_s -> portion_aps.mean_std_dev * x * delta_s -> portion_aps.mean_std_dev * x + ref_s -> portion_aps.mean_std_dev * x * ref_s -> portion_aps.mean_std_dev * x ) / delta );
    s -> scaling_error.set_stat( stat, error );
  }
}
// scaling_t::analyze_lag ===================================================

void scaling_t::analyze_lag()
{
  if ( ! calculate_scale_factors || ! scale_lag ) return;

  int num_players = ( int ) sim -> players_by_name.size();
  if ( num_players == 0 ) return;

  if ( sim -> report_progress )
  {
    util_t::fprintf( stdout, "\nGenerating scale factors for lag...\n" );
    fflush( stdout );
  }

  if ( center_scale_delta )
  {
    ref_sim = new sim_t( sim );
    ref_sim -> scaling -> scale_stat = STAT_MAX;
    ref_sim -> execute();
  }
  else
  {
    ref_sim = sim;
    ref_sim -> scaling -> scale_stat = STAT_MAX;
  }

  delta_sim = new sim_t( sim );
  delta_sim ->     gcd_lag += from_seconds( 0.100 );
  delta_sim -> channel_lag += from_seconds( 0.200 );
  delta_sim -> scaling -> scale_stat = STAT_MAX;
  delta_sim -> execute();

  for ( int i=0; i < num_players; i++ )
  {
    player_t*       p =       sim -> players_by_name[ i ];
    player_t*   ref_p =   ref_sim -> find_player( p -> name() );
    player_t* delta_p = delta_sim -> find_player( p -> name() );

    // Calculate DPS difference per millisecond of lag
    double divisor = to_millis( delta_sim -> gcd_lag - ref_sim -> gcd_lag );

    double delta_score = scale_over_function( delta_sim, delta_p );
    double   ref_score = scale_over_function(   ref_sim,   ref_p );

    double delta_error = scale_over_function_error( delta_sim, delta_p );
    double ref_error = scale_over_function_error(   ref_sim,   ref_p );
    double error = sqrt ( delta_error * delta_error + ref_error * ref_error );

    double score = ( delta_score - ref_score ) / divisor;



    if ( scale_factor_noise > 0 &&
         scale_factor_noise < error / fabs( delta_score - ref_score ) )
      sim -> errorf( "Player %s may have insufficient iterations (%d) to calculate scale factor for lag (error is >%.0f%% delta score)\n",
                     p -> name(), sim -> iterations, scale_factor_noise * 100.0 );

    error = fabs( error / divisor );
    p -> scaling_lag = score;
    p -> scaling_lag_error = error;
  }

  if ( ref_sim != sim ) delete ref_sim;
  delete delta_sim;
  delta_sim = ref_sim = 0;
}

// scaling_t::analyze_gear_weights ==========================================

void scaling_t::analyze_gear_weights()
{
  if ( num_scaling_stats <= 0 ) return;

  for ( player_t* p = sim -> player_list; p; p = p -> next )
  {
    if ( p -> quiet ) continue;

    if ( p -> is_pet() ) continue;

    chart_t::gear_weights_wowhead   ( p -> gear_weights_wowhead_link,    p );
    chart_t::gear_weights_wowreforge( p -> gear_weights_wowreforge_link, p );
    chart_t::gear_weights_pawn      ( p -> gear_weights_pawn_std_string, p, true  );
    chart_t::gear_weights_pawn      ( p -> gear_weights_pawn_alt_string, p, false );
  }
}

// scaling_t::normalize =====================================================

void scaling_t::normalize()
{
  if ( num_scaling_stats <= 0 ) return;

  for ( player_t* p = sim -> player_list; p; p = p -> next )
  {
    if ( p -> quiet ) continue;

    double divisor = p -> scaling.get_stat( p -> normalize_by() );

    if ( divisor == 0 ) continue;

    for ( stat_type i = STAT_NONE; i < STAT_MAX; i++ )
    {
      if ( ! p -> scales_with[ i ] ) continue;

      p -> scaling_normalized.set_stat( i, p -> scaling.get_stat( i ) / divisor );

      if ( normalize_scale_factors ) // For report purposes.
      {
        p -> scaling_error.set_stat( i, p -> scaling_error.get_stat( i ) / divisor );
      }
    }
  }
}

// scaling_t::analyze =======================================================

void scaling_t::analyze()
{
  if ( sim -> canceled ) return;
  init_deltas();
  analyze_stats();
  analyze_lag();
  analyze_gear_weights();
  normalize();

  for ( player_t* p = sim -> player_list; p; p = p -> next )
  {
    if ( p -> quiet ) continue;

    chart_t::scale_factors( p -> scale_factors_chart, p, p -> name_str, p -> scaling, p -> scaling_error );

    // Sort scaling results
    for ( stat_type i = STAT_NONE; i < STAT_MAX; i++ )
    {
      if ( p -> scales_with[ i ] )
      {
        double s = p -> scaling.get_stat( i );

        if ( s > 0 ) p -> scaling_stats.push_back( i );
      }
    }

    boost::sort( p -> scaling_stats, compare_scale_factors( p ) );
  }
}

// scaling_t::create_options ================================================

void scaling_t::create_options()
{
  option_t scaling_options[] =
  {
    // @option_doc loc=global/scale_factors title="Scale Factors"
    { "calculate_scale_factors",        OPT_BOOL,   &( calculate_scale_factors              ) },
    { "smooth_scale_factors",           OPT_BOOL,   &( smooth_scale_factors                 ) },
    { "normalize_scale_factors",        OPT_FUNC,   ( void* ) ::parse_normalize_scale_factors },
    { "debug_scale_factors",            OPT_BOOL,   &( debug_scale_factors                  ) },
    { "center_scale_delta",             OPT_BOOL,   &( center_scale_delta                   ) },
    { "scale_delta_multiplier",         OPT_FLT,    &( scale_delta_multiplier               ) }, // multiplies all default scale deltas
    { "positive_scale_delta",           OPT_BOOL,   &( positive_scale_delta                 ) },
    { "scale_lag",                      OPT_BOOL,   &( scale_lag                            ) },
    { "scale_factor_noise",             OPT_FLT,    &( scale_factor_noise                   ) },
    { "scale_strength",                 OPT_FLT,    &( stats.attribute[ ATTR_STRENGTH  ]    ) },
    { "scale_aim",                      OPT_FLT,    &( stats.attribute[ ATTR_AIM       ]    ) },
    { "scale_cunning",                  OPT_FLT,    &( stats.attribute[ ATTR_CUNNING   ]    ) },
    { "scale_willpower",                OPT_FLT,    &( stats.attribute[ ATTR_WILLPOWER ]    ) },
    { "scale_endurance",                OPT_FLT,    &( stats.attribute[ ATTR_ENDURANCE ]    ) },
    { "scale_presence",                 OPT_FLT,    &( stats.attribute[ ATTR_PRESENCE  ]    ) },
    { "scale_expertise_rating",         OPT_FLT,    &( stats.expertise_rating               ) },
    { "scale_accuracy_rating",          OPT_FLT,    &( stats.accuracy_rating                ) },
    { "scale_crit_rating",              OPT_FLT,    &( stats.crit_rating                    ) },
    { "scale_alacrity_rating",          OPT_FLT,    &( stats.alacrity_rating                ) },
    { "scale_power",                    OPT_FLT,    &( stats.power                          ) },
    { "scale_surge_rating",             OPT_FLT,    &( stats.surge_rating                   ) },
    { "scale_defense_rating",           OPT_FLT,    &( stats.defense_rating                 ) },
    { "scale_shield_rating",            OPT_FLT,    &( stats.shield_rating                  ) },
    { "scale_absorb_rating",            OPT_FLT,    &( stats.absorb_rating                  ) },
    { "scale_weapon_dmg",               OPT_FLT,    &( stats.weapon_dmg                     ) },
    { "scale_offhand_weapon_dmg",       OPT_FLT,    &( stats.weapon_offhand_dmg             ) },
    { "scale_only",                     OPT_STRING, &( scale_only_str                       ) },
    { "scale_alacrity_iterations",      OPT_FLT,    &( scale_alacrity_iterations            ) },
    { "scale_expertise_iterations",     OPT_FLT,    &( scale_expertise_iterations           ) },
    { "scale_crit_iterations",          OPT_FLT,    &( scale_crit_iterations                ) },
    { "scale_accuracy_iterations",      OPT_FLT,    &( scale_accuracy_iterations            ) },
    { "scale_over",                     OPT_STRING, &( scale_over                           ) },
    { "scale_over_player",              OPT_STRING, &( scale_over_player                    ) },
    { NULL, OPT_UNKNOWN, NULL }
  };

  option_t::copy( sim -> options, scaling_options );
}

// scaling_t::has_scale_factors =============================================

bool scaling_t::has_scale_factors()
{
  if ( ! calculate_scale_factors ) return false;

  for ( stat_type i = STAT_NONE; i < STAT_MAX; i++ )
  {
    if ( stats.get_stat( i ) != 0 )
    {
      return true;
    }
  }

  return false;
}

// scaling_t::scale_over_function ===========================================

double scaling_t::scale_over_function( sim_t* s, player_t* p )
{
  if ( scale_over == "raid_dps"       ) return s -> raid_dps.mean;

  player_t* q = 0;
  if ( ! scale_over_player.empty() )
    q = s -> find_player( scale_over_player );
  if ( !q )
    q = p;

  if ( scale_over == "deaths"         ) return -100 * q -> deaths.size() / s -> iterations;
  if ( scale_over == "min_death_time" ) return q -> deaths.min * 1000;
  if ( scale_over == "avg_death_time" ) return q -> deaths.mean * 1000;
  if ( scale_over == "dmg_taken"      ) return -1.0 * q -> dmg_taken.mean;
  if ( scale_over == "dtps"           ) return -1.0 * q -> dtps.mean;
  if ( scale_over == "stddev"         ) return q -> dps.std_dev;

  if ( ( q -> primary_role() == ROLE_HEAL || scale_over =="hps" ) && scale_over != "dps" )
    return p -> hps.mean;

  return p -> dps.mean;
}


// scaling_t::scale_over_function_error =====================================

double scaling_t::scale_over_function_error( sim_t* s, player_t* p )
{
  if ( scale_over == "raid_dps"       ) return s -> raid_dps.mean_std_dev;
  if ( scale_over == "min_death_time" ) return 0;
  if ( scale_over == "avg_death_time" ) return 0;
  if ( scale_over == "stddev"         ) return 0;

  player_t* q = 0;
  if ( ! scale_over_player.empty() )
    q = s -> find_player( scale_over_player );
  if ( !q )
    q = p;
  if ( scale_over == "dtps"   ) return q -> dtps.mean_std_dev;
  if ( scale_over == "deaths" ) return q -> deaths.mean_std_dev;
  if ( scale_over == "dmg_taken"      ) return q -> dmg_taken.mean_std_dev;

  if ( ( q -> primary_role() == ROLE_HEAL || scale_over =="hps" ) && scale_over != "dps" )
    return p -> hps.mean_std_dev;

  return q -> dps.mean_std_dev;
}

