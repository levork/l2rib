/*
 * Modified glass - takes an eta and an (ignored) refraysamples.
 */
surface glass(float  Ka = 0,
	      Kd = 0,
	      Ks = .6,
	      Kr = 0.5,
	      roughness = .025,
	      eta = 1.33,
	      refrraysamples = 0;
	      color  specularcolor = 1;
	      string envname = "") {

    normal Nf = normalize(N);
    vector V = normalize(I);

    color highlight = specularcolor * Ks * specular(Nf, -V, roughness);
    Ci = Os * Cs * (Ka * ambient() + Kd * diffuse(Nf)) + highlight;
    if (envname != "") {
	float Kr = 0, Kt = 1;
	vector R, T;
	fresnel (V, Nf, 1.0 / eta, Kr, Kt, R, T);
	Kt = 1 - Kr;
        R = vtransform ("world", R);
	Ci = Ci + Kr * color environment(envname, normalize(R));
    }
    Oi = Os;
}
