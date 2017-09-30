surface edgeConstant()
{
    Oi = Os;
    uniform color Ce;
    if (attribute("user:edgecolor", Ce)) {
	Ci = Ce;
    } else {
	Ci = color(1, 0, 0);
    }
}
