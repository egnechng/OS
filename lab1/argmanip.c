#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/*references:
    https://c-faq.com/decl/spiral.anderson.html (used to figure out how to understand pointer stuff)
    https://www.youtube.com/watch?v=4_2BEgOFd0E 
    (video helped me understand dynamically allocating memory for an array of strings char**)
*/

char **manipulate_args(int argc, const char *const *argv, int (*const manip)(int)){

    char** copied_args; 
    copied_args = malloc((argc + 1) * sizeof(char *));

    int length = 0;

    for (int i = 0; i < argc; i++){

        length = (int)strlen(argv[i]);

        copied_args[i] = malloc((length+1) * sizeof(char));
        
        for (int k = 0; k < length; k++)
        {
            char new = manip(argv[i][k]);
            copied_args[i][k] = new;
        }
        copied_args[i][length] = '\0';
    }
    copied_args[argc] = '\0';
    return copied_args;
}


void free_copied_args(char **args,...){  

    va_list ap;
    va_start(ap, args);

    char** c = args;

    while (c != NULL){
        
        int i =0;
        while (c[i] != NULL){
            char* s = c[i];
            free(s);
            i++;
        }
        free(c);
        c = va_arg(ap, char**);
    }

    va_end(ap);
}
