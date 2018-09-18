#include <stdio.h>
#include <ctype.h>

#include "platina_mk1_flags.h"

static const char *const flags[] = {
	PLATINA_MK1_FLAGS
};

int main(int argc, char **argv)
{
	int i;
	printf("%s",
	       "// Created by gen-platina-mk1-flags -- DO NOT EDIT\n"
	       "\n"
	       "package mk1\n"
		"\n"
		"const (\n"
	);
	printf("\t%c%sBit uint = iota\n", toupper(flags[0][0]), flags[0]+1);
	for (i = 1; flags[i]; i++)
		printf("\t%c%sBit\n", toupper(flags[i][0]), flags[i]+1);
	printf("%s",
		")\n"
		"\n"
		"var EthtoolFlags = []string{\n"
	);
	for (i = 0; flags[i]; i++)
		printf("\t\"%s\",\n", flags[i]);
	printf("}\n");
	return 0;
}
