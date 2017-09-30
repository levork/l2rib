light envlight(float samples = 64; string envmap = "") {
    normal Nf = faceforward(N, I, N);
    illuminate(Ps + Nf) {
	float occ = occlusion(Ps, Nf, samples);

	Cl = (1 - occ);
    }
}
