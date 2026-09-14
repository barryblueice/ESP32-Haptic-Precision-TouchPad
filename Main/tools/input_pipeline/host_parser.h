/* SDK boundary stubs; the actual parser, filters and simulated mouse run below. */
static device_config_t parser_config;
static int parser_steps, haptic_presses;
static bool haptic_button;
static uint8_t click_light_weight_threshold=80, click_midium_weight_threshold=100, click_strong_weight_threshold=130;
static void device_config_get(device_config_t *out) { *out=parser_config; }
static uint8_t device_config_rotation(void) { return parser_config.bytes[5]; }
static uint16_t device_config_x_max(void) { return device_config_rotation()&1 ? 1532:2302; }
static uint16_t device_config_y_max(void) { return device_config_rotation()&1 ? 2302:1532; }
static bool device_config_parser_boundary(void) { return false; }
static void cs40l25_surface_button_update(bool down, uint8_t setting)
{ (void)setting; if(down && !haptic_button) ++haptic_presses; haptic_button=down; }
int _fltused=0;
float sqrtf(float x) { return __builtin_sqrtf(x); }
float fabsf(float x) { return x<0 ? -x:x; }
