/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/drivers/disk.h>
#include <zephyr/kernel.h>

#include "msc_media_policy.h"

#if defined(MSC_MEDIA_POLICY_TEST_HOOK)
static msc_media_policy_publish_hook_t publish_hook;
static void *publish_hook_context;

void msc_media_policy_test_set_publish_hook(
	msc_media_policy_publish_hook_t hook, void *context)
{
	publish_hook = hook;
	publish_hook_context = context;
}

static void restrictive_publish_observed(void)
{
	if (publish_hook != NULL) {
		publish_hook(publish_hook_context);
	}
}
#endif

static enum msc_media_access check_access(struct msc_media_policy *policy,
					  bool write,
					  msc_media_status_cb_t status_cb,
					  void *context)
{
	int status;

	if (!atomic_get(&policy->medium_present)) {
		return MSC_MEDIA_ACCESS_NO_MEDIUM;
	}

	if (write && atomic_get(&policy->read_only)) {
		return MSC_MEDIA_ACCESS_WRITE_PROTECTED;
	}

	status = status_cb(context);
	if (status < 0 || (status & (DISK_STATUS_NOMEDIA | DISK_STATUS_UNINIT))) {
		return MSC_MEDIA_ACCESS_NO_MEDIUM;
	}

	if (write && (status & DISK_STATUS_WR_PROTECT)) {
		return MSC_MEDIA_ACCESS_WRITE_PROTECTED;
	}

	return MSC_MEDIA_ACCESS_ALLOWED;
}

void msc_media_policy_init(struct msc_media_policy *policy)
{
	k_mutex_init(&policy->disk_operation_lock);
	atomic_set(&policy->read_only, false);
	atomic_set(&policy->medium_present, true);
	atomic_set(&policy->initialized, true);
}

enum msc_media_access msc_media_policy_check(
	struct msc_media_policy *policy, bool write,
	msc_media_status_cb_t status_cb, void *context)
{
	return check_access(policy, write, status_cb, context);
}

enum msc_media_access msc_media_policy_begin_operation(
	struct msc_media_policy *policy, bool write,
	msc_media_status_cb_t status_cb, void *context)
{
	enum msc_media_access access;

	k_mutex_lock(&policy->disk_operation_lock, K_FOREVER);
	access = check_access(policy, write, status_cb, context);
	if (access != MSC_MEDIA_ACCESS_ALLOWED) {
		k_mutex_unlock(&policy->disk_operation_lock);
	}

	return access;
}

void msc_media_policy_end_operation(struct msc_media_policy *policy)
{
	k_mutex_unlock(&policy->disk_operation_lock);
}

int msc_media_policy_set_read_only(
	struct msc_media_policy *policy, bool read_only)
{
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	if (!atomic_get(&policy->initialized)) {
		return -EAGAIN;
	}

	atomic_set(&policy->read_only, read_only);
	if (read_only) {
#if defined(MSC_MEDIA_POLICY_TEST_HOOK)
		restrictive_publish_observed();
#endif
		k_mutex_lock(&policy->disk_operation_lock, K_FOREVER);
		k_mutex_unlock(&policy->disk_operation_lock);
	}

	return 0;
}

int msc_media_policy_set_medium_present(
	struct msc_media_policy *policy, bool present)
{
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	if (!atomic_get(&policy->initialized)) {
		return -EAGAIN;
	}

	atomic_set(&policy->medium_present, present);
	if (!present) {
#if defined(MSC_MEDIA_POLICY_TEST_HOOK)
		restrictive_publish_observed();
#endif
		k_mutex_lock(&policy->disk_operation_lock, K_FOREVER);
		k_mutex_unlock(&policy->disk_operation_lock);
	}

	return 0;
}

bool msc_media_policy_is_read_only(const struct msc_media_policy *policy)
{
	return atomic_get(&policy->read_only);
}

bool msc_media_policy_is_medium_present(const struct msc_media_policy *policy)
{
	return atomic_get(&policy->medium_present);
}
