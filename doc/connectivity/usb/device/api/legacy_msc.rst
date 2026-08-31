.. SPDX-License-Identifier: Apache-2.0

.. _legacy_usb_msc_api:

Legacy USB Mass Storage Class API
#################################

.. doxygengroup:: usb_msc_class
   :project: Zephyr
   :members:

This API applies only to the deprecated legacy USB device stack and its single
MSC logical unit. It does not apply to the USB device-next stack.

The read-only setting prevents host writes, but does not create filesystem
ownership. Reporting the medium absent is the host/firmware ownership boundary:
the legacy MSC LUN reports no medium while firmware changes its filesystem.
The composite USB device, CDC, and other functions remain active.

Use the controls in this order:

.. code-block:: text

   firmware begins ownership:
       set medium absent and wait for success
       then permit firmware filesystem modification

   firmware ends ownership:
       stop producers
       drain queues and partial buffers
       sync, close, and unmount successfully
       only then set medium present

The API does not mount, unmount, sync, close, or otherwise manage the
filesystem. The caller is responsible for that ordering before republishing the
medium.
