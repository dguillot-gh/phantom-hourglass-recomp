// ffcc patch: texture smoothing (uSmoothTex: 0 off, 1 smooth, 2 sharp, 3 xBR), spliced in before FinalColor() in 3DRenderFS.glsl.
//
// The game's textures are integer textures read with nearest sampling, which a GPU cannot filter, so the four texels around
// the point are fetched one by one (each fetch keeps the DS's repeat, flip and clamp) and blended here. Smooth is the
// usual bilinear blend. Sharp keeps every texel crisp and blends only in a band one pixel wide between texels (as wide as
// the texel is small on the screen), so a texture enlarged by the upscaling loses its stair-steps without going soft.
// Texels with alpha 0 keep the palette's colour 0 (often black or pink): the colours are weighted by their alpha so they
// do not bleed into the edge of a cut-out. When every texel is either clear or solid (all but the A3I5 and A5I3 formats) the
// result is clear or solid too: the cut-out is cut along the smooth blend's 0.5 contour (also in Sharp, so its edge is a clean
// diagonal, not texel-sized steps), and the opaque and translucent passes get the same kind of pixels as before.
uniform int uSmoothTex;

// xBR (uSmoothTex 3): Hyllian's xBR level 2 pixel-art scaler (the family of xBRZ), worked out for each pixel from where it
// falls in its texel, so it needs no fixed scale factor and follows the upscaling and the polygon's size on the screen.
// Each of the texel's four corners looks at the texels around it (21 in all, through the same sampler: the DS's repeat,
// flip and clamp) and finds whether an edge runs across that corner: at 45 degrees, or shallower or steeper (level 2,
// 1:2 slopes). Beyond the edge the pixel takes the colour of the neighbour on its side; the edge itself is blended over
// one screen pixel. Lone texels and square corners (letters, outlines) are left square. A texel smaller than a screen
// pixel is read as the DS does. Alpha follows the same rules as Smooth and Sharp: colours weighted by alpha, and a
// texture of clear and solid texels gives clear or solid pixels (its cut-outs follow the xBR shape).
vec4 XbrFetch(vec2 cell, vec2 size, float layer)
{
    return vec4(texture(CurTexture, vec3((cell + 0.5) / size, layer))) / vec4(63.0, 63.0, 63.0, 31.0);
}

float XbrDiff(vec4 a, vec4 b)  // YUV distance (luma counts most); clear texels differ only in alpha
{
    vec3 d = a.rgb - b.rgb;
    vec3 yuv = vec3(dot(d, vec3(0.299, 0.587, 0.114)), dot(d, vec3(-0.169, -0.331, 0.5)), dot(d, vec3(0.5, -0.419, -0.081)));
    return dot(abs(yuv), vec3(1.0, 0.25, 0.25)) * min(a.a, b.a) + abs(a.a - b.a);
}

bool XbrNeq(vec4 a, vec4 b) { return XbrDiff(a, b) > 0.06; }

// One corner of texel E, turned so that F is the neighbour across it horizontally, H vertically and I diagonally (B, D:
// E's other neighbours; C, G: beside F and H; F4, I4, H5, I5: one further out). q: the pixel in the texel, turned the
// same way (the corner at 1, 1). px: texel size of a screen pixel. Returns the corner's weight; col: its colour.
float XbrCorner(vec4 E, vec4 F, vec4 H, vec4 I, vec4 B, vec4 D, vec4 C, vec4 G, vec4 F4, vec4 I4, vec4 H5, vec4 I5,
                vec2 q, float px, out vec4 col)
{
    col = (XbrDiff(E, F) <= XbrDiff(E, H)) ? F : H;
    bool restricted = XbrNeq(E, F) && XbrNeq(E, H) &&
        ((XbrNeq(F, B) && XbrNeq(H, D)) ||
         (!XbrNeq(E, I) && XbrNeq(F, I4) && XbrNeq(H, I5)) || !XbrNeq(E, G) || !XbrNeq(E, C));
    if (!restricted) return 0.0;
    float wd1 = XbrDiff(E, C) + XbrDiff(E, G) + XbrDiff(I, F4) + XbrDiff(I, H5) + 4.0 * XbrDiff(H, F);
    float wd2 = XbrDiff(H, D) + XbrDiff(H, I5) + XbrDiff(F, I4) + XbrDiff(F, B) + 4.0 * XbrDiff(E, I);
    if (!(wd1 < wd2)) return 0.0;
    float hf = XbrDiff(F, G), vc = XbrDiff(H, C);
    bool shallow = 2.0 * hf <= vc && XbrNeq(E, G) && XbrNeq(D, G);
    bool steep = hf >= 2.0 * vc && XbrNeq(E, C) && XbrNeq(B, C);
    // Signed distance (in texels) past each edge line, over a screen pixel: 45 degrees through the middles of the two
    // sides at the corner; shallow from the middle of F's side to G's corner; steep from the middle of H's side to C's.
    float w = clamp((q.x + q.y - 1.5) / (1.41421 * px) + 0.5, 0.0, 1.0);
    if (shallow) w = max(w, clamp((q.y + 0.5 * q.x - 1.0) / (1.11803 * px) + 0.5, 0.0, 1.0));
    if (steep) w = max(w, clamp((q.x + 0.5 * q.y - 1.0) / (1.11803 * px) + 0.5, 0.0, 1.0));
    return w;
}

vec4 XbrTexel(vec3 tc)
{
    vec2 size = vec2(textureSize(CurTexture, 0).xy);
    vec2 p = tc.xy * size;
    vec2 cell = floor(p);
    vec2 fp = p - cell;
    float L = tc.z;
    vec2 fw = fwidth(p);
    float px = max(max(fw.x, fw.y), 1.0/64.0);
    vec4 e = XbrFetch(cell, size, L);
    if (px >= 1.0) return e;  // (the texture is shown at its size or smaller: the DS's own nearest texel)

    // The 5 x 5 texels around E, but its corners (xBR's names: rows a1 b1 c1 / a0 a b c c4 / d0 d e f f4 /
    // g0 g h i i4 / g5 h5 i5).
    vec4 a1 = XbrFetch(cell + vec2(-1.0, -2.0), size, L), b1 = XbrFetch(cell + vec2(0.0, -2.0), size, L),
         c1 = XbrFetch(cell + vec2(1.0, -2.0), size, L);
    vec4 a0 = XbrFetch(cell + vec2(-2.0, -1.0), size, L), a = XbrFetch(cell + vec2(-1.0, -1.0), size, L),
         b = XbrFetch(cell + vec2(0.0, -1.0), size, L), c = XbrFetch(cell + vec2(1.0, -1.0), size, L),
         c4 = XbrFetch(cell + vec2(2.0, -1.0), size, L);
    vec4 d0 = XbrFetch(cell + vec2(-2.0, 0.0), size, L), d = XbrFetch(cell + vec2(-1.0, 0.0), size, L),
         f = XbrFetch(cell + vec2(1.0, 0.0), size, L), f4 = XbrFetch(cell + vec2(2.0, 0.0), size, L);
    vec4 g0 = XbrFetch(cell + vec2(-2.0, 1.0), size, L), g = XbrFetch(cell + vec2(-1.0, 1.0), size, L),
         h = XbrFetch(cell + vec2(0.0, 1.0), size, L), i = XbrFetch(cell + vec2(1.0, 1.0), size, L),
         i4 = XbrFetch(cell + vec2(2.0, 1.0), size, L);
    vec4 g5 = XbrFetch(cell + vec2(-1.0, 2.0), size, L), h5 = XbrFetch(cell + vec2(0.0, 2.0), size, L),
         i5 = XbrFetch(cell + vec2(1.0, 2.0), size, L);

    // The four corners: the one that reaches this pixel most wins.
    vec4 col, best = e;
    float wbest = 0.0, wk;
    wk = XbrCorner(e, f, h, i, b, d, c, g, f4, i4, h5, i5, fp, px, col);                                    // bottom right
    if (wk > wbest) { wbest = wk; best = col; }
    wk = XbrCorner(e, d, h, g, b, f, a, i, d0, g0, h5, g5, vec2(1.0 - fp.x, fp.y), px, col);                // bottom left
    if (wk > wbest) { wbest = wk; best = col; }
    wk = XbrCorner(e, f, b, c, h, d, i, a, f4, c4, b1, c1, vec2(fp.x, 1.0 - fp.y), px, col);                // top right
    if (wk > wbest) { wbest = wk; best = col; }
    wk = XbrCorner(e, d, b, a, h, f, g, c, d0, a0, b1, a1, vec2(1.0 - fp.x, 1.0 - fp.y), px, col);          // top left
    if (wk > wbest) { wbest = wk; best = col; }

    float we = (1.0 - wbest) * e.a, wb = wbest * best.a;
    vec3 rgb = (we + wb > 1.0/1024.0) ? (e.rgb * we + best.rgb * wb) / (we + wb) : e.rgb;
    float alpha = mix(e.a, best.a, wbest);
    bool solid = (e.a == 0.0 || e.a == 1.0) && (best.a == 0.0 || best.a == 1.0);
    if (solid) alpha = alpha >= 0.5 ? 1.0 : 0.0;
    return vec4(rgb, alpha);
}


vec4 SmoothTexel(vec3 tc)
{
    if (uSmoothTex == 3) return XbrTexel(tc);
    vec2 size = vec2(textureSize(CurTexture, 0).xy);
    vec2 p = tc.xy * size - 0.5;
    vec2 base = floor(p);
    vec2 f = p - base;
    vec2 fs = f;  // sharpened blend for the colours; the shape of a cut-out always follows the smooth blend
    if (uSmoothTex == 2)
    {
        vec2 w = clamp(fwidth(tc.xy * size), vec2(1.0/256.0), vec2(1.0));
        fs = clamp((f - 0.5) / w + 0.5, 0.0, 1.0);
    }

    vec2 o = (base + 0.5) / size;
    vec2 d = 1.0 / size;
    vec4 t00 = vec4(texture(CurTexture, vec3(o, tc.z)));
    vec4 t10 = vec4(texture(CurTexture, vec3(o + vec2(d.x, 0.0), tc.z)));
    vec4 t01 = vec4(texture(CurTexture, vec3(o + vec2(0.0, d.y), tc.z)));
    vec4 t11 = vec4(texture(CurTexture, vec3(o + d, tc.z)));

    vec4 a4 = vec4(t00.a, t10.a, t01.a, t11.a) / 31.0;
    vec4 w4 = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    float cover = dot(w4, a4);
    vec4 ws = vec4((1.0 - fs.x) * (1.0 - fs.y), fs.x * (1.0 - fs.y), (1.0 - fs.x) * fs.y, fs.x * fs.y);
    if (dot(ws, a4) < 1.0/64.0) ws = w4;
    vec4 wa = ws * a4;
    vec3 rgb = (wa.x * t00.rgb + wa.y * t10.rgb + wa.z * t01.rgb + wa.w * t11.rgb) / max(dot(wa, vec4(1.0)), 1.0/1024.0) / 63.0;
    if (cover < 1.0/1024.0) rgb = t00.rgb / 63.0;

    bool solid = all(equal(a4 * (1.0 - a4), vec4(0.0)));  // every texel is 0 or 31 (a4 is 0 or 1)
    float a = solid ? (cover >= 0.5 ? 1.0 : 0.0) : cover;
    return vec4(rgb, a);
}

