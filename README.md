# My Shell
Implements a single command-line shell that is able to handle both interactive and batch modes. The shell takes up to one argument. 
When given one argument, it is run in batch mode and when no arugements are given, it runs in interactive mode. 

To compile: When in the directory containing all the sources files, type into the terminal “make” to create executables.
To run: To run “mysh.c”, type in the terminal “./mysh” to run in interactive mode or do “./mysh < ‘test_file’” or “./mysh ‘test_file’” to run in batch mode.

It can handle commands containing wildcards as well as multiple pipes and commands involving the home directory. 
