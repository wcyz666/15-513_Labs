/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include "cachelab.h"
#include "contracts.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. The REQUIRES and ENSURES from 15-122 are included
 *     for your convenience. They can be removed if you like.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
	int i, j, i1, j1, d0, d1, d2, d3, d4, d5, d6, d7;
	
	REQUIRES(M > 0);
    REQUIRES(N > 0);
	
/* 
 *  For a 32*32 matrix, the eviction happens every eight lines.
 * 
 *  Solution for this sub-part contains four steps:
 *		1. Partition: The whole matrix is partitioned into 16 8*8 matrices.
 *  	2. Diagonal matrices: use a local variable to cache the elements 
 *      on the diagonal line. Others simply transpose
 *      3. Non-diagonal matrices: simply transpose.
 *		(287)
 */
	if (M == 32) {	
		for (i = 0; i < N; i += 8) {
			for (j = 0; j < M; j += 8) {
			
				if (i == j) {
					for (i1 = i; i1 < i + 8; i1++) {
						for (j1 = j; j1 < j + 8; j1++) {
							if (j1 == i1) {
								d1 = A[i1][i1];
							}
							else
								B[j1][i1] = A[i1][j1];
						}
						B[i1][i1] = d1;
					}
				}	

				else {
					for (i1 = i; i1 < i + 8; i1++) {
						for (j1 = j; j1 < j + 8; j1++) {
							B[i1][j1] = A[j1][i1];
						}
					}	
				}
			}
		}
	}
/* 
 *  For a 64*64 matrix, Now the eviction happens every four lines.
 * 
 *  Solution for this sub-part contains four steps:
 *		1. Partition: The whole matrix is partitioned into 64 8*8 matrices.
 *  	2. Transpose and Copy. 
 *			(1) For the first 4*4 matrix, we can directly transpose it.
 *			(2) For the second 4*4 matrix, we copied it to the same place in B
 *			and transpose it in place.
 *		3. Copy and transpose
 *			(1) For the third 4*4 matrix, we first transmit four integers 
 *			vertically by local variables, and also copy four integers
 *			horizontally using local variables.
 *			(2) Then we put the values stored in 8 local variables to the right
 *			places.
 *		(4) Transpose. just as the first matrix.
 *		(1337)
 *	Version 1.0.1: The fourth phase has been optimized using line copy. This 
 *  can still be optimized (expand the first phase), but I have already get 
 *  full mark on this problem so let it be. (1337 -> 1291)
 *  Version 1.0.2: I will optimize it once I drop out from top 10...
 */
    if (M == 64) {
		for (i = 0; i < N; i += 8) {
			for (j = 0; j < M; j += 8) {
				for (i1 = i; i1 < i + 4; i1++) {
					for (j1 = j; j1 < j + 4; j1++) {
						B[j1][i1] = A[i1][j1];
						B[j1][i1 + 4] = A[i1][j1 + 4];
					}
				}
				for (i1 = i; i1 < i + 4; i1++) {
				
					d0 = A[i + 4][i1 - i + j];
					d1 = A[i + 5][i1 - i + j];
					d2 = A[i + 6][i1 - i + j];
					d3 = A[i + 7][i1 - i + j];

					d4 = B[i1 - i + j][i + 4];
					d5 = B[i1 - i + j][i + 5];
					d6 = B[i1 - i + j][i + 6];
					d7 = B[i1 - i + j][i + 7];
				
					B[i1 - i + j][i + 4] = d0;
					B[i1 - i + j][i + 5] = d1;
					B[i1 - i + j][i + 6] = d2;
					B[i1 - i + j][i + 7] = d3;

					B[i1 - i + j + 4][i] = d4;
					B[i1 - i + j + 4][i + 1] = d5;
					B[i1 - i + j + 4][i + 2] = d6;
					B[i1 - i + j + 4][i + 3] = d7;
					
				}
				if (i != j) {
					for (i1 = i + 4; i1 < i + 8; i1++) {

						d0 = A[i1][j + 4];
						d1 = A[i1][j + 5];
						d2 = A[i1][j + 6];
						d3 = A[i1][j + 7];
					
						B[j + 4][i1] = d0;
						B[j + 5][i1] = d1;
						B[j + 6][i1] = d2;
						B[j + 7][i1] = d3;
		
					}
				}
				else {	
					for (i1 = i + 4; i1 < i + 8; i1++) {
						for (j1 = j + 4; j1 < j + 8; j1++) {
							if (j1 == i1) {
								d0 = A[i1][i1];
							}
							else {
								B[j1][i1] = A[i1][j1];
							}
						}
						B[i1][i1] = d0;
					}
				}
			}
		}
	}
/*  Well, no tricks, just enumerate all situations....
 *  First test matrix n*n, find that 15*15 and 16*16 is smallest (1970)
 *	Then set m = 16/15, test matrix m*n, find that 16*1 and 15*1 is the
 *  smallest. Then they are the answer.....(1816)
 *
 *  Version 1.0.1: Expand 16 * 1 assignment (1816 -> 1746)
 *  Version 1.0.2: Expand the rest part (1746 -> 1732)
 *
 *  That's fun....	
*/	
	if (M == 61) {
		for (i = 0; i < 61; i += 16) {
			for (j = 0; j < 67; j += 1) {
				if (i == 48) {
					d0 = A[j][i];
					d1 = A[j][i + 1];
					d2 = A[j][i + 2];
					d3 = A[j][i + 3];
					d4 = A[j][i + 4];
					d5 = A[j][i + 5];
					d6 = A[j][i + 6];
					d7 = A[j][i + 7];
					
					B[i][j] = d0;
					B[i + 1][j] = d1;
					B[i + 2][j] = d2;
					B[i + 3][j] = d3;	
					B[i + 4][j] = d4;	
					B[i + 5][j] = d5;	
					B[i + 6][j] = d6;	
					B[i + 7][j] = d7;		

					d0 = A[j][i + 8];
					d1 = A[j][i + 9];
					d2 = A[j][i + 10];
					d3 = A[j][i + 11];
					d4 = A[j][i + 12];

					
					B[i + 8][j] = d0;
					B[i + 9][j] = d1;
					B[i + 10][j] = d2;
					B[i + 11][j] = d3;	
					B[i + 12][j] = d4;	

				}
				else {
					d0 = A[j][i];
					d1 = A[j][i + 1];
					d2 = A[j][i + 2];
					d3 = A[j][i + 3];
					d4 = A[j][i + 4];
					d5 = A[j][i + 5];
					d6 = A[j][i + 6];
					d7 = A[j][i + 7];
					
					B[i][j] = d0;
					B[i + 1][j] = d1;
					B[i + 2][j] = d2;
					B[i + 3][j] = d3;	
					B[i + 4][j] = d4;	
					B[i + 5][j] = d5;	
					B[i + 6][j] = d6;	
					B[i + 7][j] = d7;		

					d0 = A[j][i + 8];
					d1 = A[j][i + 9];
					d2 = A[j][i + 10];
					d3 = A[j][i + 11];
					d4 = A[j][i + 12];
					d5 = A[j][i + 13];
					d6 = A[j][i + 14];
					d7 = A[j][i + 15];
					
					B[i + 8][j] = d0;
					B[i + 9][j] = d1;
					B[i + 10][j] = d2;
					B[i + 11][j] = d3;	
					B[i + 12][j] = d4;	
					B[i + 13][j] = d5;	
					B[i + 14][j] = d6;	
					B[i + 15][j] = d7;	
				}				
			} 
		}	
	}
	
    ENSURES(is_transpose(M, N, A, B));
}


/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */ 

/* 
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    REQUIRES(M > 0);
    REQUIRES(N > 0);

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    

    ENSURES(is_transpose(M, N, A, B));
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc); 

/*    
 *  A memorization for enumeration

    registerTransFunction(trans3, trans_desc3); 
    registerTransFunction(trans4, trans_desc4); 
    registerTransFunction(trans5, trans_desc5); 
    registerTransFunction(trans6, trans_desc6); 
    registerTransFunction(trans7, trans_desc7); 
    registerTransFunction(trans9, trans_desc9); 
    registerTransFunction(trans10, trans_desc10); 
    registerTransFunction(trans11, trans_desc11); 
    registerTransFunction(trans12, trans_desc12); 
    registerTransFunction(trans13, trans_desc13); 
    registerTransFunction(trans14, trans_desc14); 
    registerTransFunction(trans15, trans_desc15); 
    registerTransFunction(trans16, trans_desc16); 
    registerTransFunction(trans17, trans_desc17); 
    registerTransFunction(trans18, trans_desc18); 
    registerTransFunction(trans19, trans_desc19); 
    registerTransFunction(trans20, trans_desc20); 
    registerTransFunction(trans21, trans_desc21); 
    registerTransFunction(trans22, trans_desc22); 
    registerTransFunction(trans23, trans_desc23); 
    registerTransFunction(trans24, trans_desc24); 
    registerTransFunction(trans25, trans_desc25); 
    registerTransFunction(trans26, trans_desc26); */
}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

