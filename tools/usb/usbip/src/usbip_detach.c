/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <unistd.h>

#include "vhci_driver.h"
#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip.h"
#include "utils.h"

static const char usbip_detach_usage_string[] =
	"usbip detach <args>\n"
        "    -i, --vhci-ix=<ix>   index of the "
        USBIP_VHCI_DRV_NAME
        " the device is on (defaults to 0)\n"
        "    -p, --port=<port>    port the device is on\n";

void usbip_detach_usage(void)
{
	printf("usage: %s", usbip_detach_usage_string);
}

static int detach_port(int vhci_ix, int port)
{
	int ret;
	char path[PATH_MAX+1];

	/* remove the port state file */

	snprintf(path, PATH_MAX, VHCI_STATE_PATH"/port%d-%d", vhci_ix, port);

	remove(path);
	rmdir(VHCI_STATE_PATH);

	ret = usbip_vhci_driver_open_ix(vhci_ix);
	if (ret < 0) {
		err("open vhci_driver");
		return -1;
	}

	ret = usbip_vhci_detach_device(port);
	if (ret < 0)
		return -1;

	usbip_vhci_driver_close();

	return ret;
}

int usbip_detach(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "port", required_argument, NULL, 'p' },
                { "vhci-ix", 0, NULL, 'i' },
		{ NULL, 0, NULL, 0 }
	};
	int opt;
        int port = -1;
        int vhci_ix = 0;

	for (;;) {
		opt = getopt_long(argc, argv, "p:i:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'p':
                        if (atoi_with_check(optarg, &port) < 0) {
                                err("Bad port number");
                                return -1;
                        }
                        break;
                case 'i':
                        if (atoi_with_check(optarg, &vhci_ix) < 0) {
                                err("Bax vhci index");
                                return -1;
                        }
                        break;
		default:
			goto err_out;
		}
	}
        if (optind < argc)
                goto err_out;

        if (port < 0)
                goto err_out;

        return detach_port(vhci_ix, port);
err_out:
        usbip_detach_usage();
        return -1;
}
