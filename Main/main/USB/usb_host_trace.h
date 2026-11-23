#pragma once

#if USB_HOST_TRACE_ENABLED
void usb_host_trace_init(void);
#else
static inline void usb_host_trace_init(void) {}
#endif
