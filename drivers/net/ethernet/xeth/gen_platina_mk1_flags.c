#include <stdio.h>
#include <ctype.h>

extern const char *const platina_mk1_flags[];

int main(int argc, char **argv)
{
	int i;
	printf("%s",
	       "// Created by gen-platina-mk1-flags -- DO NOT EDIT\n"
	       "\n"
	       "package main\n"
		"\n"
		"const (\n"
	);
	printf("\t%c%sBit uint = iota\n", toupper(platina_mk1_flags[0][0]),
	       platina_mk1_flags[0]+1);
	for (i = 1; platina_mk1_flags[i]; i++)
		printf("\t%c%sBit\n", toupper(platina_mk1_flags[i][0]),
		       platina_mk1_flags[i]+1);
	printf("%s",
		")\n"
		"\n"
		"var flags = []string{\n"
	);
	for (i = 0; platina_mk1_flags[i]; i++)
		printf("\t\"%s\",\n", platina_mk1_flags[i]);
	printf("}\n");
	return 0;
}
