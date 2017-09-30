displacement basicDisp(string map = "lego.tx";
			float Kd = 0.1)
{
    if( map != "" )
    {
        float bmp = Kd * float texture(map[0], s, t);
	float s2c_scale = length(ntransform("shader",N))/length(N);
	point PP = P;
	PP += bmp * normalize (N) * s2c_scale;
	normal deltaN = normalize(N) - normalize(Ng);
	N = normalize(calculatenormal(PP)) + deltaN;
    }
}
