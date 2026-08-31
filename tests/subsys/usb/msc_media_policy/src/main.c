/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/disk.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include "msc_media_policy.h"

#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY K_PRIO_PREEMPT(1)
#define TEST_TIMEOUT K_SECONDS(1)

struct fake_disk {
	int status;
	atomic_t status_calls;
	atomic_t read_calls;
	atomic_t write_calls;
	atomic_t sync_calls;
};

static void fake_disk_reset(struct fake_disk *disk, int status)
{
	disk->status = status;
	atomic_set(&disk->status_calls, 0);
	atomic_set(&disk->read_calls, 0);
	atomic_set(&disk->write_calls, 0);
	atomic_set(&disk->sync_calls, 0);
}

static int fake_disk_status(void *context)
{
	struct fake_disk *disk = context;

	atomic_inc(&disk->status_calls);
	return disk->status;
}

static void fake_disk_read(struct fake_disk *disk)
{
	atomic_inc(&disk->read_calls);
}

static void fake_disk_write(struct fake_disk *disk)
{
	atomic_inc(&disk->write_calls);
}

static void fake_disk_sync(struct fake_disk *disk)
{
	atomic_inc(&disk->sync_calls);
}

static void assert_fake_counts(const struct fake_disk *disk, int status,
				       int read, int write, int sync)
{
	zassert_equal(atomic_get(&disk->status_calls), status,
		      "status calls: expected %d", status);
	zassert_equal(atomic_get(&disk->read_calls), read,
		      "read calls: expected %d", read);
	zassert_equal(atomic_get(&disk->write_calls), write,
		      "write calls: expected %d", write);
	zassert_equal(atomic_get(&disk->sync_calls), sync,
		      "sync calls: expected %d", sync);
}

static enum msc_media_access perform_operation(struct msc_media_policy *policy,
						struct fake_disk *disk, bool write)
{
	enum msc_media_access access;

	access = msc_media_policy_check(policy, write, fake_disk_status, disk);
	if (access != MSC_MEDIA_ACCESS_ALLOWED) {
		return access;
	}

	access = msc_media_policy_begin_operation(policy, write, fake_disk_status, disk);
	if (access != MSC_MEDIA_ACCESS_ALLOWED) {
		return access;
	}

	if (write) {
		fake_disk_write(disk);
		fake_disk_sync(disk);
	} else {
		fake_disk_read(disk);
	}
	msc_media_policy_end_operation(policy);

	return MSC_MEDIA_ACCESS_ALLOWED;
}

ZTEST(msc_media_policy, test_defaults_allow_read_and_write)
{
	struct msc_media_policy policy;
	struct fake_disk disk;

	msc_media_policy_init(&policy);
	zassert_true(msc_media_policy_is_medium_present(&policy));
	zassert_false(msc_media_policy_is_read_only(&policy));

	fake_disk_reset(&disk, DISK_STATUS_OK);
	zassert_equal(perform_operation(&policy, &disk, false),
		      MSC_MEDIA_ACCESS_ALLOWED);
	assert_fake_counts(&disk, 2, 1, 0, 0);

	fake_disk_reset(&disk, DISK_STATUS_OK);
	zassert_equal(perform_operation(&policy, &disk, true),
		      MSC_MEDIA_ACCESS_ALLOWED);
	assert_fake_counts(&disk, 2, 0, 1, 1);
}

ZTEST(msc_media_policy, test_runtime_read_only_blocks_write_without_disk_access)
{
	struct msc_media_policy policy;
	struct fake_disk disk;

	msc_media_policy_init(&policy);
	zassert_ok(msc_media_policy_set_read_only(&policy, true));

	fake_disk_reset(&disk, DISK_STATUS_OK);
	zassert_equal(perform_operation(&policy, &disk, true),
		      MSC_MEDIA_ACCESS_WRITE_PROTECTED);
	assert_fake_counts(&disk, 0, 0, 0, 0);
}

ZTEST(msc_media_policy, test_medium_absent_blocks_read_and_write_without_disk_access)
{
	struct msc_media_policy policy;
	struct fake_disk disk;

	msc_media_policy_init(&policy);
	zassert_ok(msc_media_policy_set_medium_present(&policy, false));

	fake_disk_reset(&disk, DISK_STATUS_OK);
	zassert_equal(perform_operation(&policy, &disk, false),
		      MSC_MEDIA_ACCESS_NO_MEDIUM);
	zassert_equal(perform_operation(&policy, &disk, true),
		      MSC_MEDIA_ACCESS_NO_MEDIUM);
	assert_fake_counts(&disk, 0, 0, 0, 0);
}

ZTEST(msc_media_policy, test_backing_disk_status_is_respected)
{
	struct msc_media_policy policy;
	struct fake_disk disk;

	msc_media_policy_init(&policy);

	fake_disk_reset(&disk, DISK_STATUS_OK);
	zassert_equal(msc_media_policy_check(&policy, false, fake_disk_status, &disk),
		      MSC_MEDIA_ACCESS_ALLOWED);
	assert_fake_counts(&disk, 1, 0, 0, 0);

	fake_disk_reset(&disk, DISK_STATUS_OK);
	zassert_equal(msc_media_policy_check(&policy, true, fake_disk_status, &disk),
		      MSC_MEDIA_ACCESS_ALLOWED);
	assert_fake_counts(&disk, 1, 0, 0, 0);
}

ZTEST(msc_media_policy, test_backing_disk_write_protect_allows_read_and_blocks_write)
{
	struct msc_media_policy policy;
	struct fake_disk disk;

	msc_media_policy_init(&policy);

	fake_disk_reset(&disk, DISK_STATUS_WR_PROTECT);
	zassert_equal(perform_operation(&policy, &disk, false),
		      MSC_MEDIA_ACCESS_ALLOWED);
	assert_fake_counts(&disk, 2, 1, 0, 0);

	fake_disk_reset(&disk, DISK_STATUS_WR_PROTECT);
	zassert_equal(perform_operation(&policy, &disk, true),
		      MSC_MEDIA_ACCESS_WRITE_PROTECTED);
	assert_fake_counts(&disk, 1, 0, 0, 0);
}

ZTEST(msc_media_policy, test_disk_nomedia_uninit_and_negative_status_are_no_medium)
{
	struct msc_media_policy policy;
	struct fake_disk disk;
	const int unavailable_statuses[] = {
		DISK_STATUS_NOMEDIA,
		DISK_STATUS_UNINIT,
		-EIO,
	};

	msc_media_policy_init(&policy);

	for (size_t i = 0; i < ARRAY_SIZE(unavailable_statuses); ++i) {
		fake_disk_reset(&disk, unavailable_statuses[i]);
		zassert_equal(msc_media_policy_check(&policy, false, fake_disk_status, &disk),
			      MSC_MEDIA_ACCESS_NO_MEDIUM);
		assert_fake_counts(&disk, 1, 0, 0, 0);
	}
}

ZTEST(msc_media_policy, test_preinit_setters_fail)
{
	struct msc_media_policy policy = { 0 };

	zassert_equal(msc_media_policy_set_read_only(&policy, true), -EAGAIN);
	zassert_equal(msc_media_policy_set_medium_present(&policy, false), -EAGAIN);
}

struct isr_setter_result {
	struct msc_media_policy *policy;
	bool in_isr;
	int read_only_result;
	int medium_present_result;
};

static void call_setters_from_isr(const void *parameter)
{
	struct isr_setter_result *result = (struct isr_setter_result *)parameter;

	result->in_isr = k_is_in_isr();
	result->read_only_result = msc_media_policy_set_read_only(result->policy, true);
	result->medium_present_result =
		msc_media_policy_set_medium_present(result->policy, false);
}

ZTEST(msc_media_policy, test_isr_setters_fail)
{
	struct msc_media_policy policy;
	struct isr_setter_result result = {
		.policy = &policy,
	};

	msc_media_policy_init(&policy);
	irq_offload(call_setters_from_isr, &result);

	zassert_true(result.in_isr);
	zassert_equal(result.read_only_result, -EWOULDBLOCK);
	zassert_equal(result.medium_present_result, -EWOULDBLOCK);
	zassert_false(msc_media_policy_is_read_only(&policy));
	zassert_true(msc_media_policy_is_medium_present(&policy));
}

ZTEST(msc_media_policy, test_write_protection_state_is_independent_of_presence)
{
	struct msc_media_policy policy;
	struct fake_disk disk;

	msc_media_policy_init(&policy);
	zassert_ok(msc_media_policy_set_read_only(&policy, true));
	zassert_ok(msc_media_policy_set_medium_present(&policy, false));
	zassert_true(msc_media_policy_is_read_only(&policy));
	zassert_false(msc_media_policy_is_medium_present(&policy));

	fake_disk_reset(&disk, DISK_STATUS_OK);
	zassert_equal(msc_media_policy_check(&policy, false, fake_disk_status, &disk),
		      MSC_MEDIA_ACCESS_NO_MEDIUM);
	zassert_equal(msc_media_policy_check(&policy, true, fake_disk_status, &disk),
		      MSC_MEDIA_ACCESS_NO_MEDIUM);
	assert_fake_counts(&disk, 0, 0, 0, 0);

	zassert_ok(msc_media_policy_set_read_only(&policy, false));
	zassert_false(msc_media_policy_is_read_only(&policy));
	zassert_false(msc_media_policy_is_medium_present(&policy));
	zassert_equal(msc_media_policy_check(&policy, false, fake_disk_status, &disk),
		      MSC_MEDIA_ACCESS_NO_MEDIUM);
	assert_fake_counts(&disk, 0, 0, 0, 0);

	zassert_ok(msc_media_policy_set_read_only(&policy, true));
	zassert_ok(msc_media_policy_set_medium_present(&policy, true));
	zassert_true(msc_media_policy_is_read_only(&policy));
	zassert_true(msc_media_policy_is_medium_present(&policy));
	zassert_equal(msc_media_policy_check(&policy, true, fake_disk_status, &disk),
		      MSC_MEDIA_ACCESS_WRITE_PROTECTED);
	assert_fake_counts(&disk, 0, 0, 0, 0);
}

enum setter_kind {
	SET_READ_ONLY,
	SET_MEDIUM_PRESENT,
};

struct concurrency_context {
	struct msc_media_policy policy;
	struct fake_disk disk;
	struct k_sem active_entered;
	struct k_sem release_active;
	struct k_sem active_done;
	struct k_sem queued_admitted;
	struct k_sem queued_begin;
	struct k_sem queued_done;
	struct k_sem setter_done;
	struct k_sem published;
	struct k_thread active_thread;
	struct k_thread queued_thread;
	struct k_thread setter_thread;
	enum msc_media_access active_result;
	enum msc_media_access queued_result;
	int active_gate_result;
	int queued_gate_result;
	int setter_result;
	bool active_write;
	bool queued_write;
	enum setter_kind setter_kind;
	bool setter_value;
};

K_THREAD_STACK_DEFINE(active_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(queued_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(setter_stack, THREAD_STACK_SIZE);

static struct concurrency_context concurrency;

static void concurrency_init(struct concurrency_context *context)
{
	memset(context, 0, sizeof(*context));
	fake_disk_reset(&context->disk, DISK_STATUS_OK);
	msc_media_policy_init(&context->policy);
	k_sem_init(&context->active_entered, 0, 1);
	k_sem_init(&context->release_active, 0, 1);
	k_sem_init(&context->active_done, 0, 1);
	k_sem_init(&context->queued_admitted, 0, 1);
	k_sem_init(&context->queued_begin, 0, 1);
	k_sem_init(&context->queued_done, 0, 1);
	k_sem_init(&context->setter_done, 0, 1);
	k_sem_init(&context->published, 0, 1);
}

static void perform_fake_operation(struct concurrency_context *context, bool write)
{
	if (write) {
		fake_disk_write(&context->disk);
		fake_disk_sync(&context->disk);
	} else {
		fake_disk_read(&context->disk);
	}
}

static void active_operation(void *parameter, void *unused1, void *unused2)
{
	struct concurrency_context *context = parameter;
	enum msc_media_access access;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	access = msc_media_policy_check(&context->policy, context->active_write,
					fake_disk_status, &context->disk);
	if (access == MSC_MEDIA_ACCESS_ALLOWED) {
		access = msc_media_policy_begin_operation(&context->policy,
				context->active_write, fake_disk_status, &context->disk);
	}

	if (access == MSC_MEDIA_ACCESS_ALLOWED) {
		k_sem_give(&context->active_entered);
		context->active_gate_result = k_sem_take(&context->release_active,
							 TEST_TIMEOUT);
		if (context->active_gate_result == 0) {
			perform_fake_operation(context, context->active_write);
		}
		msc_media_policy_end_operation(&context->policy);
	}

	context->active_result = access;
	k_sem_give(&context->active_done);
}

static void queued_operation(void *parameter, void *unused1, void *unused2)
{
	struct concurrency_context *context = parameter;
	enum msc_media_access access;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	access = msc_media_policy_check(&context->policy, context->queued_write,
					fake_disk_status, &context->disk);
	k_sem_give(&context->queued_admitted);
	if (access == MSC_MEDIA_ACCESS_ALLOWED) {
		context->queued_gate_result = k_sem_take(&context->queued_begin,
							 TEST_TIMEOUT);
		if (context->queued_gate_result == 0) {
			access = msc_media_policy_begin_operation(&context->policy,
				context->queued_write, fake_disk_status, &context->disk);
			if (access == MSC_MEDIA_ACCESS_ALLOWED) {
				perform_fake_operation(context, context->queued_write);
				msc_media_policy_end_operation(&context->policy);
			}
		}
	}

	context->queued_result = access;
	k_sem_give(&context->queued_done);
}

static void setter_operation(void *parameter, void *unused1, void *unused2)
{
	struct concurrency_context *context = parameter;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	if (context->setter_kind == SET_READ_ONLY) {
		context->setter_result = msc_media_policy_set_read_only(&context->policy,
				context->setter_value);
	} else {
		context->setter_result = msc_media_policy_set_medium_present(&context->policy,
				context->setter_value);
	}
	k_sem_give(&context->setter_done);
}

static void restrictive_publish_hook(void *parameter)
{
	struct concurrency_context *context = parameter;

	k_sem_give(&context->published);
}

static void start_active_operation(struct concurrency_context *context)
{
	k_thread_create(&context->active_thread, active_stack,
			K_THREAD_STACK_SIZEOF(active_stack), active_operation, context,
			NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
}

static void start_queued_operation(struct concurrency_context *context)
{
	k_thread_create(&context->queued_thread, queued_stack,
			K_THREAD_STACK_SIZEOF(queued_stack), queued_operation, context,
			NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
}

static void start_setter_operation(struct concurrency_context *context)
{
	k_thread_create(&context->setter_thread, setter_stack,
			K_THREAD_STACK_SIZEOF(setter_stack), setter_operation, context,
			NULL, NULL, THREAD_PRIORITY, 0, K_NO_WAIT);
}

static bool take_with_timeout(struct k_sem *semaphore)
{
	return k_sem_take(semaphore, TEST_TIMEOUT) == 0;
}

static bool setter_has_not_completed(struct concurrency_context *context)
{
	return k_sem_take(&context->setter_done, K_NO_WAIT) != 0;
}

struct thread_cleanup_result {
	bool completion_failed;
	bool initial_join_failed;
	bool reap_failed;
};

static void cleanup_created_thread(struct k_sem *done, bool done_seen,
				   struct k_thread *thread,
				   struct thread_cleanup_result *result)
{
	int join_result;

	if (!done_seen && !take_with_timeout(done)) {
		result->completion_failed = true;
	}

	join_result = k_thread_join(thread, TEST_TIMEOUT);
	if (join_result != 0) {
		result->initial_join_failed = true;

		/*
		 * k_thread_abort() does not return until its target can no longer
		 * execute. Reap that terminated thread before any test state is
		 * reused, while retaining the original bounded-join failure.
		 */
		k_thread_abort(thread);
		if (k_thread_join(thread, K_NO_WAIT) != 0) {
			result->reap_failed = true;
		}
	}
}

static bool complete_and_join(struct concurrency_context *context, bool active_started,
				      bool queued_started, bool setter_started,
				      bool active_done_seen, bool queued_done_seen,
				      bool setter_done_seen, bool clear_publish_hook)
{
	struct thread_cleanup_result result = { 0 };

	k_sem_give(&context->release_active);
	k_sem_give(&context->queued_begin);

	if (active_started) {
		cleanup_created_thread(&context->active_done, active_done_seen,
				       &context->active_thread, &result);
	}
	if (queued_started) {
		cleanup_created_thread(&context->queued_done, queued_done_seen,
				       &context->queued_thread, &result);
	}
	if (setter_started) {
		cleanup_created_thread(&context->setter_done, setter_done_seen,
				       &context->setter_thread, &result);
	}

	if (clear_publish_hook) {
		msc_media_policy_test_set_publish_hook(NULL, NULL);
	}

	return !result.completion_failed && !result.initial_join_failed &&
	       !result.reap_failed;
}

ZTEST(msc_media_policy, test_absent_transition_waits_for_active_operation)
{
	struct concurrency_context *context = &concurrency;
	bool active_entered;
	bool published;
	bool setter_blocked;
	bool completed;

	concurrency_init(context);
	context->setter_kind = SET_MEDIUM_PRESENT;
	context->setter_value = false;
	msc_media_policy_test_set_publish_hook(restrictive_publish_hook, context);

	start_active_operation(context);
	active_entered = take_with_timeout(&context->active_entered);
	start_setter_operation(context);
	published = take_with_timeout(&context->published);
	setter_blocked = setter_has_not_completed(context);
	completed = complete_and_join(context, true, false, true, false, false, false,
				      true);

	zassert_true(active_entered && published && setter_blocked && completed,
		     "active operation or restrictive setter did not synchronize");
	zassert_equal(context->active_result, MSC_MEDIA_ACCESS_ALLOWED);
	zassert_equal(context->active_gate_result, 0);
	zassert_equal(context->setter_result, 0);
	zassert_false(msc_media_policy_is_medium_present(&context->policy));
	assert_fake_counts(&context->disk, 2, 1, 0, 0);
}

ZTEST(msc_media_policy, test_read_only_transition_waits_for_active_write)
{
	struct concurrency_context *context = &concurrency;
	bool active_entered;
	bool published;
	bool setter_blocked;
	bool completed;

	concurrency_init(context);
	context->active_write = true;
	context->setter_kind = SET_READ_ONLY;
	context->setter_value = true;
	msc_media_policy_test_set_publish_hook(restrictive_publish_hook, context);

	start_active_operation(context);
	active_entered = take_with_timeout(&context->active_entered);
	start_setter_operation(context);
	published = take_with_timeout(&context->published);
	setter_blocked = setter_has_not_completed(context);
	completed = complete_and_join(context, true, false, true, false, false, false,
				      true);

	zassert_true(active_entered && published && setter_blocked && completed,
		     "active write or restrictive setter did not synchronize");
	zassert_equal(context->active_result, MSC_MEDIA_ACCESS_ALLOWED);
	zassert_equal(context->active_gate_result, 0);
	zassert_equal(context->setter_result, 0);
	zassert_true(msc_media_policy_is_read_only(&context->policy));
	assert_fake_counts(&context->disk, 2, 0, 1, 1);
}

ZTEST(msc_media_policy, test_queued_read_is_rejected_after_absence_publication)
{
	struct concurrency_context *context = &concurrency;
	bool queued_admitted;
	bool active_entered;
	bool published;
	bool setter_blocked;
	bool completed;

	concurrency_init(context);
	context->setter_kind = SET_MEDIUM_PRESENT;
	context->setter_value = false;

	start_queued_operation(context);
	queued_admitted = take_with_timeout(&context->queued_admitted);
	start_active_operation(context);
	active_entered = take_with_timeout(&context->active_entered);
	msc_media_policy_test_set_publish_hook(restrictive_publish_hook, context);
	start_setter_operation(context);
	published = take_with_timeout(&context->published);
	setter_blocked = setter_has_not_completed(context);
	k_sem_give(&context->queued_begin);
	completed = complete_and_join(context, true, true, true, false, false, false,
				      true);

	zassert_true(queued_admitted && active_entered && published && setter_blocked &&
		     completed, "queued read did not synchronize");
	zassert_equal(context->active_result, MSC_MEDIA_ACCESS_ALLOWED);
	zassert_equal(context->active_gate_result, 0);
	zassert_equal(context->queued_result, MSC_MEDIA_ACCESS_NO_MEDIUM);
	zassert_equal(context->queued_gate_result, 0);
	zassert_equal(context->setter_result, 0);
	assert_fake_counts(&context->disk, 3, 1, 0, 0);
}

ZTEST(msc_media_policy, test_queued_write_is_rejected_after_read_only_publication)
{
	struct concurrency_context *context = &concurrency;
	bool queued_admitted;
	bool active_entered;
	bool published;
	bool setter_blocked;
	bool completed;

	concurrency_init(context);
	context->active_write = true;
	context->queued_write = true;
	context->setter_kind = SET_READ_ONLY;
	context->setter_value = true;

	start_queued_operation(context);
	queued_admitted = take_with_timeout(&context->queued_admitted);
	start_active_operation(context);
	active_entered = take_with_timeout(&context->active_entered);
	msc_media_policy_test_set_publish_hook(restrictive_publish_hook, context);
	start_setter_operation(context);
	published = take_with_timeout(&context->published);
	setter_blocked = setter_has_not_completed(context);
	k_sem_give(&context->queued_begin);
	completed = complete_and_join(context, true, true, true, false, false, false,
				      true);

	zassert_true(queued_admitted && active_entered && published && setter_blocked &&
		     completed, "queued write did not synchronize");
	zassert_equal(context->active_result, MSC_MEDIA_ACCESS_ALLOWED);
	zassert_equal(context->active_gate_result, 0);
	zassert_equal(context->queued_result, MSC_MEDIA_ACCESS_WRITE_PROTECTED);
	zassert_equal(context->queued_gate_result, 0);
	zassert_equal(context->setter_result, 0);
	assert_fake_counts(&context->disk, 3, 0, 1, 1);
}

ZTEST(msc_media_policy, test_permissive_transitions_do_not_wait)
{
	struct concurrency_context *context = &concurrency;
	bool active_entered;
	bool setter_completed;
	bool completed;

	concurrency_init(context);
	zassert_ok(msc_media_policy_set_read_only(&context->policy, true));
	context->setter_kind = SET_READ_ONLY;
	context->setter_value = false;
	start_active_operation(context);
	active_entered = take_with_timeout(&context->active_entered);
	start_setter_operation(context);
	setter_completed = take_with_timeout(&context->setter_done);
	completed = complete_and_join(context, true, false, true, false, false, true,
				      false);

	zassert_true(active_entered && setter_completed && completed,
		     "read-only clear waited for the active operation");
	zassert_equal(context->active_result, MSC_MEDIA_ACCESS_ALLOWED);
	zassert_equal(context->active_gate_result, 0);
	zassert_equal(context->setter_result, 0);
	zassert_false(msc_media_policy_is_read_only(&context->policy));
	assert_fake_counts(&context->disk, 2, 1, 0, 0);

	concurrency_init(context);
	context->setter_kind = SET_MEDIUM_PRESENT;
	context->setter_value = true;
	start_active_operation(context);
	active_entered = take_with_timeout(&context->active_entered);
	start_setter_operation(context);
	setter_completed = take_with_timeout(&context->setter_done);
	completed = complete_and_join(context, true, false, true, false, false, true,
				      false);

	zassert_true(active_entered && setter_completed && completed,
		     "medium-present republish waited for the active operation");
	zassert_equal(context->active_result, MSC_MEDIA_ACCESS_ALLOWED);
	zassert_equal(context->active_gate_result, 0);
	zassert_equal(context->setter_result, 0);
	zassert_true(msc_media_policy_is_medium_present(&context->policy));
	assert_fake_counts(&context->disk, 2, 1, 0, 0);
}

ZTEST_SUITE(msc_media_policy, NULL, NULL, NULL, NULL, NULL);
