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
