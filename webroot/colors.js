function HSVtoRGB(h, s, v) {
    var r, g, b, i, f, p, q, t;
    if (arguments.length === 1) {
        s = h.s, v = h.v, h = h.h;
    }
    i = Math.floor(h * 6);
    f = h * 6 - i;
    p = v * (1 - s);
    q = v * (1 - f * s);
    t = v * (1 - (1 - f) * s);
    switch (i % 6) {
        case 0:
            r = v, g = t, b = p;
            break;
        case 1:
            r = q, g = v, b = p;
            break;
        case 2:
            r = p, g = v, b = t;
            break;
        case 3:
            r = p, g = q, b = v;
            break;
        case 4:
            r = t, g = p, b = v;
            break;
        case 5:
            r = v, g = p, b = q;
            break;
    }
    return {
        r: Math.round(r * 255),
        g: Math.round(g * 255),
        b: Math.round(b * 255)
    };
}

// from http://martin.ankerl.com/2009/12/09/how-to-create-random-colors-programmatically/
//rgb(46, 204, 113) rgb(52, 152, 219) rgb(241, 196, 15) rgb(231, 76, 60)
var id2Rgb = function (s) {
    var golden_ratio_conjugate = 0.618033988749895;
    var h = ((s.hashCode()) % 47) / 47;
    h = 0.5 + h / 2;
    h += golden_ratio_conjugate
    h %= 1
   // console.log(h);
    var c = HSVtoRGB(h, 0.4, 0.9);

    return 'rgb(' + c.r + ',' + c.g + ',' + c.b + ')';
    //console.log('rgb(' + c.r + ',' +c.g+',' + c.b +')');
    //return '#'+(Math.abs(s.hashCode()) % 0xffffff).toString(16);
    //return pastelColors(Math.abs(s.hashCode()));
};
