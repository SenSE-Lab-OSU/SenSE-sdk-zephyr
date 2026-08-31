/*
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Legacy USB Mass Storage Class API
 */

#ifndef ZEPHYR_INCLUDE_USB_CLASS_USB_MSC_H_
#define ZEPHYR_INCLUDE_USB_CLASS_USB_MSC_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Legacy USB Mass Storage Class API
 * @defgroup usb_msc_class Legacy USB Mass Storage Class API
 * @ingroup usb
 * @{
 */

/**
 * @brief Set host write protection for the legacy USB MSC LUN.
 *
 * Available when @kconfig{CONFIG_USB_MASS_STORAGE} is enabled. This is a
 * thread-context API and must not be called from an ISR. It controls only host
 * access through the legacy single LUN; it does not change backing-disk or
 * firmware filesystem access.
 *
 * Setting @p read_only to true publishes write protection before waiting for a
 * protected host data operation to finish, including a write's required sync.
 * After a successful return, no new conflicting backing-disk write can begin.
 * Clearing write protection only permits future host writes; the caller remains
 * responsible for filesystem ownership and ordering.
 *
 * The setting persists across USB and Bulk-Only Transport resets, disconnects,
 * reconnects, and suspend/resume events.
 *
 * @param read_only True to reject host writes, false to permit future host
 *                  writes when the backing disk permits them.
 *
 * @retval 0 On success.
 * @retval -EWOULDBLOCK Called from ISR context.
 * @retval -EAGAIN Called before legacy MSC initialization completes.
 */
int usb_mass_storage_set_read_only(bool read_only);

/**
 * @brief Set media presence for the legacy USB MSC LUN.
 *
 * Available when @kconfig{CONFIG_USB_MASS_STORAGE} is enabled. This is a
 * thread-context API and must not be called from an ISR. It controls only host
 * access through the legacy single LUN; it does not mount, unmount, sync,
 * close, or otherwise change firmware filesystem access.
 *
 * Setting @p present to false publishes absence before waiting for a protected
 * host data operation to finish. After a successful return, no new
 * conflicting backing-disk data operation can begin. Setting @p present to
 * true permits only future host access; callers must establish a safe
 * filesystem state before republishing the medium.
 *
 * The setting persists across USB and Bulk-Only Transport resets, disconnects,
 * reconnects, and suspend/resume events. It does not affect CDC or other
 * composite USB functions.
 *
 * @param present True to publish the medium, false to report it absent.
 *
 * @retval 0 On success.
 * @retval -EWOULDBLOCK Called from ISR context.
 * @retval -EAGAIN Called before legacy MSC initialization completes.
 */
int usb_mass_storage_set_medium_present(bool present);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_USB_CLASS_USB_MSC_H_ */
