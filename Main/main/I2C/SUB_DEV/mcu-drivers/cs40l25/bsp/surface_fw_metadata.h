/* SurfaceTouchpadHaptic_2.9.139, sdk_surface fw_img_v2.
 * Registers absent from the 33-entry generic symbol table are derived from
 * cs40l25_ext_boost_0A0603.surface.wmfw: VIBEGEN algorithm 0xBD,
 * XM base 723 words, YM base 0; control offsets 0, 1, 3, 5 and 0.
 * These are bus addresses, never fw_img symbol IDs.
 */
#ifndef SURFACE_FW_METADATA_H
#define SURFACE_FW_METADATA_H

#define SURFACE_FW_SIZE_BYTES             38576U
#define SURFACE_FW_ID                     0x1400E1U
#define SURFACE_FW_REVISION               0x0A0603U
#define SURFACE_FW_SHA256 "f93d5b1058b09f11c7d2a28560d92c43a2407f4012293a1e3c39a574215d3cda"
#define SURFACE_FW_XM_WAVES               25U
#define SURFACE_FW_YM_WAVES               53U
#define SURFACE_FW_NUM_WAVES              (SURFACE_FW_XM_WAVES + SURFACE_FW_YM_WAVES)
#define SURFACE_VIBEGEN_ENABLE_REG        0x02800B4CU
#define SURFACE_VIBEGEN_STATUS_REG        0x02800B50U
#define SURFACE_VIBEGEN_NUM_WAVES_REG     0x02800B58U
#define SURFACE_VIBEGEN_WAVETABLE_XM_REG  0x02800B60U
#define SURFACE_VIBEGEN_WAVETABLE_YM_REG  0x03400000U

#endif
