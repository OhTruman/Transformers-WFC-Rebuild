// Presentation-health measures for the fidelity gate (compiled by Add-Type). Catastrophic-failure detectors, not
// screenshot recognition: they must reject a frame whose world is black / missing / blank / smeared even when the HUD,
// the player character or a sky is drawn.
//
// All measures work on 8x8 pixel blocks inside a region given in fractions of the frame (x0, y0, x1, y1).
//   black      fraction of pixels with luma < 12
//   detail     fraction of blocks with luma std-dev >= 6   (textured / structured content)
//   flat       fraction of blocks with luma std-dev < 2.5  (featureless: blank, fog, flat untextured polygon, empty sky)
//   maxFlat    largest 4-connected component of flat blocks with a similar mean colour, as a fraction of the region
//              (one giant blank / grey / black area, or one giant untextured / malformed polygon)
//   streak     fraction of textured blocks whose gradient energy is > 12x stronger along one axis (stretched textures,
//              degenerate triangles smeared across the screen)
//   edges      fraction of pixels with a luma gradient > 24
//   mean, sat  mean luma, mean saturation
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;

public static class WfcPresent {
    static float[] LoadLuma(string path, out int w, out int h, out float[] sat) {
        using (var bmp = new Bitmap(path)) {
            w = bmp.Width; h = bmp.Height;
            var L = new float[w * h]; sat = new float[w * h];
            var data = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                var row = new byte[data.Stride];
                for (int y = 0; y < h; y++) {
                    System.Runtime.InteropServices.Marshal.Copy(data.Scan0 + y * data.Stride, row, 0, data.Stride);
                    for (int x = 0; x < w; x++) {
                        int o = x * 3; float b = row[o], g = row[o + 1], r = row[o + 2];
                        L[y * w + x] = 0.114f * b + 0.587f * g + 0.299f * r;
                        float mx = Math.Max(r, Math.Max(g, b)), mn = Math.Min(r, Math.Min(g, b));
                        sat[y * w + x] = mx > 0 ? (mx - mn) / mx : 0;
                    }
                }
            } finally { bmp.UnlockBits(data); }
            return L;
        }
    }

    // returns { black, detail, flat, maxFlat, streak, edges, mean, sat }
    public static double[] Measure(string path, double fx0, double fy0, double fx1, double fy1) {
        int w, h; float[] S; float[] L = LoadLuma(path, out w, out h, out S);
        const int B = 8;
        int x0 = (int)(fx0 * w) / B, y0 = (int)(fy0 * h) / B, x1 = (int)(fx1 * w) / B, y1 = (int)(fy1 * h) / B;
        int bw = Math.Max(1, x1 - x0), bh = Math.Max(1, y1 - y0), nb = bw * bh;
        var bMean = new float[nb]; var bStd = new float[nb]; var isFlat = new bool[nb];
        int black = 0, edges = 0, pix = 0, detail = 0, flat = 0, streak = 0, textured = 0; double sumL = 0, sumS = 0;
        for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
            int px0 = (x0 + bx) * B, py0 = (y0 + by) * B; double s = 0, s2 = 0, gx = 0, gy = 0;
            for (int y = py0; y < py0 + B && y < h; y++) for (int x = px0; x < px0 + B && x < w; x++) {
                float l = L[y * w + x]; s += l; s2 += l * l; pix++; sumL += l; sumS += S[y * w + x];
                if (l < 12) black++;
                if (x + 1 < w && y + 1 < h) {
                    float dx = L[y * w + x + 1] - l, dy = L[(y + 1) * w + x] - l;
                    gx += dx * dx; gy += dy * dy;
                    if (Math.Abs(dx) + Math.Abs(dy) > 24) edges++;
                }
            }
            int i = by * bw + bx; float m = (float)(s / (B * B)); float sd = (float)Math.Sqrt(Math.Max(0, s2 / (B * B) - m * m));
            bMean[i] = m; bStd[i] = sd;
            if (sd >= 6) { detail++; textured++; double hi = Math.Max(gx, gy), lo = Math.Min(gx, gy); if (hi > 12 * (lo + 1)) streak++; }
            if (sd < 2.5f) { flat++; isFlat[i] = true; }
        }
        // largest connected flat area of similar mean colour
        var seen = new bool[nb]; int maxComp = 0; var stack = new Stack<int>();
        for (int i = 0; i < nb; i++) {
            if (!isFlat[i] || seen[i]) continue;
            int n = 0; seen[i] = true; stack.Push(i); float m0 = bMean[i];
            while (stack.Count > 0) {
                int c = stack.Pop(); n++; int cx = c % bw, cy = c / bw;
                int[] nbr = { cx > 0 ? c - 1 : -1, cx < bw - 1 ? c + 1 : -1, cy > 0 ? c - bw : -1, cy < bh - 1 ? c + bw : -1 };
                foreach (int k in nbr) if (k >= 0 && !seen[k] && isFlat[k] && Math.Abs(bMean[k] - m0) < 12) { seen[k] = true; stack.Push(k); }
            }
            if (n > maxComp) maxComp = n;
        }
        return new double[] { (double)black / Math.Max(1, pix), (double)detail / nb, (double)flat / nb, (double)maxComp / nb,
                              textured > 0 ? (double)streak / textured : 0, (double)edges / Math.Max(1, pix), sumL / Math.Max(1, pix), sumS / Math.Max(1, pix) };
    }

    // Malformed-geometry signatures in a region: { untexturedMax, untexturedFrac, noiseFrac }.
    //   untextured: 8x8 blocks that are lit (mean luma > 30), desaturated (< 0.20) and without texture detail
    //               (mean |Laplacian| < 1.5): geometry drawn without its material / a
    //               placeholder / a giant malformed polygon. untexturedMax = largest connected such area (fraction).
    //   noise:      blocks whose |Laplacian| dominates their std-dev (pixel noise, not structure): a noise / garbage
    //               texture stretched over large polygons.
    public static double[] Malformed(string path, double fx0, double fy0, double fx1, double fy1) {
        int w, h; float[] S; float[] L = LoadLuma(path, out w, out h, out S);
        const int B = 8;
        int x0 = (int)(fx0 * w) / B, y0 = (int)(fy0 * h) / B, x1 = (int)(fx1 * w) / B, y1 = (int)(fy1 * h) / B;
        int bw = Math.Max(1, x1 - x0), bh = Math.Max(1, y1 - y0), nb = bw * bh;
        var unt = new bool[nb]; int untN = 0, noise = 0;
        for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
            int px0 = (x0 + bx) * B, py0 = (y0 + by) * B; double s = 0, s2 = 0, lap = 0, sat = 0; int n = 0, nl = 0;
            for (int y = py0; y < py0 + B && y < h; y++) for (int x = px0; x < px0 + B && x < w; x++) {
                float l = L[y * w + x]; s += l; s2 += l * l; sat += S[y * w + x]; n++;
                if (x > 0 && y > 0 && x + 1 < w && y + 1 < h) { lap += Math.Abs(4 * l - L[y * w + x - 1] - L[y * w + x + 1] - L[(y - 1) * w + x] - L[(y + 1) * w + x]); nl++; }
            }
            if (n == 0) continue;
            double m = s / n, sd = Math.Sqrt(Math.Max(0, s2 / n - m * m)), lp = nl > 0 ? lap / nl : 0, st = sat / n;
            int i = by * bw + bx;
            if (m > 30 && st < 0.20 && lp < 1.5) { unt[i] = true; untN++; }
            if (sd >= 6 && lp > 2.6 * sd) noise++;
        }
        var seen = new bool[nb]; int maxComp = 0; var stack = new Stack<int>();
        for (int i = 0; i < nb; i++) {
            if (!unt[i] || seen[i]) continue;
            int c0 = 0; seen[i] = true; stack.Push(i);
            while (stack.Count > 0) { int c = stack.Pop(); c0++; int cx = c % bw, cy = c / bw;
                int[] nbr = { cx > 0 ? c - 1 : -1, cx < bw - 1 ? c + 1 : -1, cy > 0 ? c - bw : -1, cy < bh - 1 ? c + bw : -1 };
                foreach (int k in nbr) if (k >= 0 && !seen[k] && unt[k]) { seen[k] = true; stack.Push(k); } }
            if (c0 > maxComp) maxComp = c0;
        }
        return new double[] { (double)maxComp / nb, (double)untN / nb, (double)noise / nb };
    }

    // Structural similarity of two frames in a region: { lumaCorr, gradCorr, detailIoU } on a 4-px grid.
    // lumaCorr: correlation of block means (same layout / lighting); gradCorr: correlation of block gradient energy
    // (same architecture edges); detailIoU: overlap of the textured-block masks (world present in the same places).
    public static double[] Similarity(string a, string b, double fx0, double fy0, double fx1, double fy1) {
        int wa, ha, wb, hb; float[] sa, sb; float[] La = LoadLuma(a, out wa, out ha, out sa), Lb = LoadLuma(b, out wb, out hb, out sb);
        const int G = 64, H = 36;
        var ma = Grid(La, wa, ha, G, H, fx0, fy0, fx1, fy1, false); var mb = Grid(Lb, wb, hb, G, H, fx0, fy0, fx1, fy1, false);
        var ga = Grid(La, wa, ha, G, H, fx0, fy0, fx1, fy1, true); var gb = Grid(Lb, wb, hb, G, H, fx0, fy0, fx1, fy1, true);
        int inter = 0, uni = 0;
        for (int i = 0; i < ga.Length; i++) { bool da = ga[i] > 6, db = gb[i] > 6; if (da && db) inter++; if (da || db) uni++; }
        return new double[] { Corr(ma, mb), Corr(ga, gb), uni > 0 ? (double)inter / uni : 1.0 };
    }
    static double[] Grid(float[] L, int w, int h, int G, int H, double fx0, double fy0, double fx1, double fy1, bool grad) {
        var o = new double[G * H]; int rx0 = (int)(fx0 * w), ry0 = (int)(fy0 * h), rw = (int)((fx1 - fx0) * w), rh = (int)((fy1 - fy0) * h);
        for (int gy = 0; gy < H; gy++) for (int gx = 0; gx < G; gx++) {
            int px0 = rx0 + gx * rw / G, px1 = rx0 + (gx + 1) * rw / G, py0 = ry0 + gy * rh / H, py1 = ry0 + (gy + 1) * rh / H; double s = 0; int n = 0;
            for (int y = py0; y < py1 && y + 1 < h; y++) for (int x = px0; x < px1 && x + 1 < w; x++) {
                float l = L[y * w + x];
                if (grad) s += Math.Abs(L[y * w + x + 1] - l) + Math.Abs(L[(y + 1) * w + x] - l); else s += l;
                n++;
            }
            o[gy * G + gx] = n > 0 ? s / n : 0;
        }
        return o;
    }
    static double Corr(double[] a, double[] b) {
        int n = a.Length; double ma = 0, mb = 0; for (int i = 0; i < n; i++) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
        double sab = 0, saa = 0, sbb = 0; for (int i = 0; i < n; i++) { double da = a[i] - ma, db = b[i] - mb; sab += da * db; saa += da * da; sbb += db * db; }
        return (saa < 1e-6 || sbb < 1e-6) ? 0 : sab / Math.Sqrt(saa * sbb);
    }
}
