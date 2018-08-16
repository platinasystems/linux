#include <stdio.h>

#include "platina_mk1_stats.h"

static const char *const stats[] = {
	PLATINA_MK1_STATS
};

int main(int argc, char **argv)
{
	int i;
	printf("// Created by gen-platina-mk1-stats -- DO NOT EDIT\n");
	printf("\n");
	printf("package main\n");
	printf("\n");
	printf("var stats = []string{\n");
	for (i = 0; stats[i]; i++)
		printf("\t\"%s\",\n", stats[i]);
	printf("}\n");
	return 0;
}
