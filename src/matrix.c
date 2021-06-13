#include <stdio.h>

void
print_matrix(float m[9])
{
	printf(" [%.2f  %.2f  %.2f] \n", m[0], m[1], m[2]);
	printf(" [%.2f  %.2f  %.2f] \n", m[3], m[4], m[5]);
	printf(" [%.2f  %.2f  %.2f] \n\n", m[6], m[7], m[8]);
}

void
multiply_matrices(float a[9], float b[9], float out[9])
{
	// zero out target matrix
	for (int i = 0; i < 9; i++) {
		out[i] = 0;
	}

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			for (int k = 0; k < 3; k++) {
				out[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
			}
		}
	}
}
