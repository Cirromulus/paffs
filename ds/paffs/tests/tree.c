#include <stdio.h>
#include <string.h>
#ifdef __WIN32__
#include <io.h>
#include <windows.h>
#endif
#include <stdlib.h>

#include "btree.h"


/* First message to the user.
 */
void usage_1( void ) {
        printf("B+ Tree of Order %d.\n", btree_order);
        printf("To build a B+ tree of a different order, start again and enter the order\n");
        printf("as an integer argument:  bpt <order>  ");
        printf("(%d <= order <= %d).\n", MIN_ORDER, MAX_ORDER);
        printf("To start with input from a file of newline-delimited integers, \n"
                        "start again and enter ");
        printf("the order followed by the filename:\n"
                        "bpt <order> <inputfile> .\n");
}


/* Second message to the user.
 */
void usage_2( void ) {
        printf("Enter any of the following commands after the prompt > :\n");
        printf("\ti <k>  -- Insert <k> (an integer) as both key and value).\n");
        printf("\tf <k>  -- Find the value under key <k>.\n");
        printf("\tp <k> -- Print the path from the root to key k and its associated value.\n");
        printf("\tr <k1> <k2> -- Print the keys and values found in the range "
                        "[<k1>, <k2>\n");
        printf("\ta -- Add new Node with the first free key.\n");
        printf("\td <k>  -- Delete key <k> and its associated value.\n");
        printf("\tx -- Destroy the whole tree.  Start again with an empty tree of the same order.\n");
        printf("\tt -- Print the B+ tree.\n");
        printf("\tl -- Print the keys of the leaves (bottom row of the tree).\n");
        printf("\tv -- Toggle output of pointer addresses (\"verbose\") in tree and leaves.\n");
        printf("\tq -- Quit. (Or use Ctl-D.)\n");
        printf("\t? -- Print this help message.\n");
}

/* Brief usage note.
 */
void usage_3( void ) {
        printf("Usage: ./bpt [<order>]\n");
        printf("\twhere %d <= order <= %d .\n", MIN_ORDER, MAX_ORDER);
}

int main( int argc, char ** argv ) {

        char * input_file;
        FILE * fp;
        node * root;
        int input, range2;
        char instruction, license_part;
        bool verbose_output = false;
        root = NULL;

        if (argc > 1) {
                btree_order = atoi(argv[1]);
                if (btree_order < MIN_ORDER || btree_order > MAX_ORDER) {
                        fprintf(stderr, "Invalid order: %d .\n\n", btree_order);
                        exit(EXIT_FAILURE);
                }
        }


        if (argc > 2) {
                input_file = argv[2];
                fp = fopen(input_file, "r");
                if (fp == NULL) {
                        perror("Failure to open input file.");
                        exit(EXIT_FAILURE);
                }
                while (!feof(fp)) {
                        fscanf(fp, "%d\n", &input);
                        pInode pn = {0};
                        pn.no = input;
                        root = insert(root, input, pn);
                }
                fclose(fp);
                print_tree(root, verbose_output);
        }

        printf("> ");
        while (scanf("%c", &instruction) != EOF) {
        		pInode new_Pinode = {0};
                switch (instruction) {
                case 'd':
                        scanf("%d", &input);
                        root = delete(root, input);
                        print_tree(root, verbose_output);
                        break;
                case 'i':
                        scanf("%d", &input);
                        new_Pinode.no = input;
                        root = insert(root, new_Pinode.no, new_Pinode);
                        print_tree(root, verbose_output);
                        break;
                case 'f':
                case 'p':
                        scanf("%d", &input);
                        find_and_print(root, input, instruction == 'p');
                        break;
                case 'r':
                        scanf("%d %d", &input, &range2);
                        if (input > range2) {
                                int tmp = range2;
                                range2 = input;
                                input = tmp;
                        }
                        find_and_print_range(root, input, range2, instruction == 'p');
                        break;
                case 'a':
                		new_Pinode.no = find_first_free_key(root);
                		printf("\nFound key: %d\n", new_Pinode.no);
                		root = insert(root, new_Pinode.no, new_Pinode);
                		print_tree(root, verbose_output);
                		break;
                case 'l':
                        print_leaves(root, verbose_output);
                        break;
                case 'q':
                        while (getchar() != (int)'\n');
                        return EXIT_SUCCESS;
                case 's':
                        if (scanf("how %c", &license_part) == 0) {
                                usage_2();
                                break;
                        }
                        break;
                case 't':
                        print_tree(root, verbose_output);
                        break;
                case 'v':
                        verbose_output = !verbose_output;
                        break;
                case 'x':
                        if (root)
                                root = destroy_tree(root);
                        print_tree(root, verbose_output);
                        break;
                default:
                        usage_2();
                        break;
                }
                while (getchar() != (int)'\n');
                printf("> ");
        }
        printf("\n");

        return EXIT_SUCCESS;
}
