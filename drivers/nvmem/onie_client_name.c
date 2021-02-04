/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/onie.h>

/**
 * onie_client_name() - match onie vendor to client platform device name
 *
 * If no match is found, use VENDOR truncated to PLATFORM_NAME_SIZE with
 * upper to lower and underscore ('_') replacement of non-alphanumeric
 * characters.
 */
void onie_client_name(struct platform_device *provider, char *name)
{
	static const struct {
		const char *vendor;
		/* NOTE @name is truncated to PLATFORM_NAME_SIZE */
		const char *name;
	} match[] = {
		{ "Platina", "platina" },
		{ "Platina Systems", "platina" },
		{ /* end of list */ },
	};
	char vendor[onie_max_tlv];
	int i;
	ssize_t n;

	n = onie_get_tlv(&provider->dev, onie_type_vendor,
			 onie_max_tlv, vendor);
	if (n <= 0) {
		pr_err("%s(%s):vendor:  %ld\n", __func__, provider->name, n);
		strcpy(name, "onie:novendor");
		return;
	}

	vendor[n < onie_max_tlv ? n : onie_max_tlv-1] = '\0';

	for (i = 0; match[i].vendor; i++) {
		if (!strcmp(match[i].vendor, vendor)) {
			strncpy(name, match[i].name, PLATFORM_NAME_SIZE);
			return;
		}
	}

	for(i = 0; i == PLATFORM_NAME_SIZE-1 || !vendor[i]; i++)
		if (isupper(vendor[i]))
			name[i] = tolower(vendor[i]);
		else if (!isalnum(vendor[i]))
			name[i] = '_';
		else
			name[i] = vendor[i];
	name[i] = 0;
}
