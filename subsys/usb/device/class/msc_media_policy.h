/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_USB_DEVICE_CLASS_MSC_MEDIA_POLICY_H_
#define ZEPHYR_SUBSYS_USB_DEVICE_CLASS_MSC_MEDIA_POLICY_H_

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

typedef int (*msc_media_status_cb_t)(void *context);

enum msc_media_access {
	MSC_MEDIA_ACCESS_ALLOWED,
	MSC_MEDIA_ACCESS_NO_MEDIUM,
	MSC_MEDIA_ACCESS_WRITE_PROTECTED,
};

struct msc_media_policy {
	atomic_t read_only;
	atomic_t medium_present;
	atomic_t initialized;
	struct k_mutex disk_operation_lock;
};

void msc_media_policy_init(struct msc_media_policy *policy);

enum msc_media_access msc_media_policy_check(
	struct msc_media_policy *policy, bool write,
	msc_media_status_cb_t status_cb, void *context);

/*
 * On MSC_MEDIA_ACCESS_ALLOWED, this function returns with
 * disk_operation_lock held. The caller must call msc_media_policy_end_operation()
 * exactly once, after the complete backing-disk operation (and write sync).
 * Rejected operations return with the lock released.
 */
enum msc_media_access msc_media_policy_begin_operation(
	struct msc_media_policy *policy, bool write,
	msc_media_status_cb_t status_cb, void *context);

void msc_media_policy_end_operation(struct msc_media_policy *policy);

int msc_media_policy_set_read_only(
	struct msc_media_policy *policy, bool read_only);

int msc_media_policy_set_medium_present(
	struct msc_media_policy *policy, bool present);

bool msc_media_policy_is_read_only(
	const struct msc_media_policy *policy);

bool msc_media_policy_is_medium_present(
	const struct msc_media_policy *policy);

#if defined(MSC_MEDIA_POLICY_TEST_HOOK)
typedef void (*msc_media_policy_publish_hook_t)(void *context);

void msc_media_policy_test_set_publish_hook(
	msc_media_policy_publish_hook_t hook, void *context);
#endif

#endif /* ZEPHYR_SUBSYS_USB_DEVICE_CLASS_MSC_MEDIA_POLICY_H_ */
