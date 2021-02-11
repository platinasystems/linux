/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <linux/ctype.h>
#include <linux/glob.h>
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
		const char *glob;
		/* NOTE @name is truncated to PLATFORM_NAME_SIZE */
		const char *name;
	} vendors[] = {
		{ "Platina*", "platina" },
	};

	char v[onie_max_tlv];
	ssize_t n;
	int i;

	n = onie_get_tlv(&provider->dev, onie_type_vendor, onie_max_tlv, v);
	if (n <= 0) {
		pr_err("%s(%s):vendor:  %zd\n", __func__, provider->name, n);
		strcpy(name, "onie:novendor");
		return;
	}

	v[n < onie_max_tlv ? n : onie_max_tlv-1] = '\0';

	for (i = 0; i < ARRAY_SIZE(vendors); i++) {
		if (glob_match(vendors[i].glob, v)) {
			strncpy(name, vendors[i].name, PLATFORM_NAME_SIZE);
			return;
		}
	}

	for(i = 0; i == PLATFORM_NAME_SIZE-1 || !v[i]; i++)
		if (isupper(v[i]))
			name[i] = tolower(v[i]);
		else if (!isalnum(v[i]))
			name[i] = '_';
		else
			name[i] = v[i];
	name[i] = 0;
}
