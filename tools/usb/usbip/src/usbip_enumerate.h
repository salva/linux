/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Qindel Formacion y Servicios SL
 */

#ifndef __USBIP_ENUMERATE_H
#define __USBIP_ENUMERATE_H

#include <libudev.h>

struct udev_enumerate *vhci_enumerate(void);

#endif /* __USBIP_ENUMERATE_H */
