#include <stdio.h>

extern const char *const platina_mk1_flags[];

int main(int argc, char **argv)
{
	int i;
	printf("// Created by gen-platina-mk1-flags -- DO NOT EDIT\n");
	printf("\n");
	printf("package main\n");
	printf("\n");
	printf("var flags = []string{\n");
	for (i = 0; platina_mk1_flags[i]; i++)
		printf("\t\"%s\",\n", platina_mk1_flags[i]);
	printf("}\n");
	return 0;
}
