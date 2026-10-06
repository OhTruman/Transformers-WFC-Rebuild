// Image statistics for the fidelity scripts (compiled by Add-Type). Works on 24/32-bit bitmaps.
using System;
using System.Drawing;
using System.Drawing.Imaging;

public static class WfcImage {
    // Downsampled luminance grid (cell x cell blocks), 0..255.
    public static float[] Luma(string path, int cell) {
        using (var bmp = new Bitmap(path)) {
            int w = bmp.Width / cell, h = bmp.Height / cell;
            var outv = new float[w * h + 2];
            outv[0] = w; outv[1] = h;
            var data = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                int stride = data.Stride;
                var row = new byte[stride];
                var acc = new double[w * h];
                for (int y = 0; y < h * cell; y++) {
                    System.Runtime.InteropServices.Marshal.Copy(data.Scan0 + y * stride, row, 0, stride);
                    int cy = y / cell;
                    for (int x = 0; x < w * cell; x++) {
                        int o = x * 3;
                        acc[cy * w + x / cell] += 0.114 * row[o] + 0.587 * row[o + 1] + 0.299 * row[o + 2];
                    }
                }
                for (int i = 0; i < w * h; i++) outv[i + 2] = (float)(acc[i] / (cell * cell));
            } finally { bmp.UnlockBits(data); }
            return outv;
        }
    }
    // Mean absolute luminance difference of two grids, and the fraction of cells changing > thr.
    public static double[] Diff(float[] a, float[] b, float thr) {
        int n = Math.Min(a.Length, b.Length);
        double s = 0; int changed = 0;
        for (int i = 2; i < n; i++) { float d = Math.Abs(a[i] - b[i]); s += d; if (d > thr) changed++; }
        return new double[] { s / (n - 2), (double)changed / (n - 2) };
    }
    // Diff restricted to a sub-rectangle given in fractions of the grid (fx0,fy0)-(fx1,fy1): same outputs as Diff.
    public static double[] RegionDiff(float[] a, float[] b, double fx0, double fy0, double fx1, double fy1, float thr) {
        int w = (int)a[0], h = (int)a[1];
        int x0 = (int)(fx0 * w), x1 = Math.Max(x0 + 1, (int)(fx1 * w)), y0 = (int)(fy0 * h), y1 = Math.Max(y0 + 1, (int)(fy1 * h));
        double s = 0; int changed = 0, n = 0;
        for (int y = y0; y < y1 && y < h; y++)
            for (int x = x0; x < x1 && x < w; x++) {
                float d = Math.Abs(a[2 + y * w + x] - b[2 + y * w + x]); s += d; n++; if (d > thr) changed++;
            }
        return new double[] { n > 0 ? s / n : 0, n > 0 ? (double)changed / n : 0 };
    }
    // Frame statistics: mean luma, fraction of near-black cells (< blackThr), fraction of flat cells
    // (|cell - right neighbour| < 0.5 and |cell - lower neighbour| < 0.5 and not black: untextured surfaces).
    public static double[] Stats(float[] a, float blackThr) {
        int w = (int)a[0], h = (int)a[1], n = w * h, black = 0, flat = 0; double s = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float v = a[2 + y * w + x]; s += v;
                if (v < blackThr) { black++; continue; }
                if (x + 1 < w && y + 1 < h && Math.Abs(v - a[2 + y * w + x + 1]) < 0.5f && Math.Abs(v - a[2 + (y + 1) * w + x]) < 0.5f) flat++;
            }
        return new double[] { s / n, (double)black / n, (double)flat / n };
    }
    // Downsampled RGB grid (cell x cell blocks), 0..255: [w, h, r0, g0, b0, r1, ...].
    public static float[] Rgb(string path, int cell) {
        using (var bmp = new Bitmap(path)) {
            int w = bmp.Width / cell, h = bmp.Height / cell;
            var outv = new float[3 * w * h + 2];
            outv[0] = w; outv[1] = h;
            var data = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                var row = new byte[data.Stride];
                var acc = new double[3 * w * h];
                for (int y = 0; y < h * cell; y++) {
                    System.Runtime.InteropServices.Marshal.Copy(data.Scan0 + y * data.Stride, row, 0, data.Stride);
                    int cy = y / cell;
                    for (int x = 0; x < w * cell; x++) {
                        int o = x * 3, c = 3 * (cy * w + x / cell);
                        acc[c] += row[o + 2]; acc[c + 1] += row[o + 1]; acc[c + 2] += row[o];
                    }
                }
                for (int i = 0; i < 3 * w * h; i++) outv[i + 2] = (float)(acc[i] / (cell * cell));
            } finally { bmp.UnlockBits(data); }
            return outv;
        }
    }
    // RGB grid -> luma grid in the Luma() layout ([w, h, l0, l1, ...]) for Diff / Stats.
    public static float[] Luma2(float[] rgb) {
        int n = (int)rgb[0] * (int)rgb[1]; var o = new float[n + 2]; o[0] = rgb[0]; o[1] = rgb[1];
        for (int i = 0; i < n; i++) o[i + 2] = L(rgb, i);
        return o;
    }
    static float L(float[] g, int i) { return 0.299f * g[2 + 3 * i] + 0.587f * g[3 + 3 * i] + 0.114f * g[4 + 3 * i]; }
    // Contribution of a skipped material / effect: cells where |with - without| luma > thr.
    // Returns [coverage, mean dR, mean dG, mean dB (signed, over the mask), mean |dL| over the mask, max |dL|].
    public static double[] MaskStats(float[] with, float[] without, float thr) {
        int n = (int)with[0] * (int)with[1], m = 0; double r = 0, g = 0, b = 0, l = 0, mx = 0;
        for (int i = 0; i < n; i++) {
            double d = Math.Abs(L(with, i) - L(without, i));
            if (d <= thr) continue;
            m++; l += d; mx = Math.Max(mx, d);
            r += with[2 + 3 * i] - without[2 + 3 * i]; g += with[3 + 3 * i] - without[3 + 3 * i]; b += with[4 + 3 * i] - without[4 + 3 * i];
        }
        return m == 0 ? new double[] { 0, 0, 0, 0, 0, 0 } : new double[] { (double)m / n, r / m, g / m, b / m, l / m, mx };
    }
    // Change over time of the contribution itself: mean |(a1-b1) - (a0-b0)| luma over cells in either mask.
    public static double MaskMotion(float[] a0, float[] b0, float[] a1, float[] b1, float thr) {
        int n = (int)a0[0] * (int)a0[1], m = 0; double s = 0;
        for (int i = 0; i < n; i++) {
            double c0 = L(a0, i) - L(b0, i), c1 = L(a1, i) - L(b1, i);
            if (Math.Abs(c0) <= thr && Math.Abs(c1) <= thr) continue;
            m++; s += Math.Abs(c1 - c0);
        }
        return m == 0 ? 0 : s / m;
    }
    // Inside a screen polygon (grid coordinates, x0,y0,x1,y1,...): [cells inside, fraction of them where the
    // material contributes (|dL| > thr), fraction NOT contributing (holes)].
    public static double[] PolyCoverage(float[] with, float[] without, double[] poly, float thr) {
        int w = (int)with[0], h = (int)with[1], inside = 0, hit = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (!InPoly(poly, x + 0.5, y + 0.5)) continue;
                inside++;
                if (Math.Abs(L(with, y * w + x) - L(without, y * w + x)) > thr) hit++;
            }
        return inside == 0 ? new double[] { 0, 0, 0 } : new double[] { inside, (double)hit / inside, 1.0 - (double)hit / inside };
    }
    // Over an explicit cell list (grid indices y*w+x): [cells, fraction contributing (|dL| > thr), fraction not].
    public static double[] CellCoverage(float[] with, float[] without, int[] cells, float thr) {
        int hit = 0, n = 0, total = (int)with[0] * (int)with[1];
        foreach (int i in cells) { if (i < 0 || i >= total) continue; n++; if (Math.Abs(L(with, i) - L(without, i)) > thr) hit++; }
        return n == 0 ? new double[] { 0, 0, 0 } : new double[] { n, (double)hit / n, 1.0 - (double)hit / n };
    }
    // Overlay variant for an explicit cell list (blue = listed cells without contribution).
    public static void OverlayCells(string withPath, float[] with, float[] without, int cell, int[] cells, float thr, string outPath) {
        int w = (int)with[0]; var set = new System.Collections.Generic.HashSet<int>(cells);
        using (var bmp = new Bitmap(withPath))
        using (var g = Graphics.FromImage(bmp)) {
            var red = new SolidBrush(Color.FromArgb(90, 255, 0, 0)); var blue = new SolidBrush(Color.FromArgb(110, 0, 80, 255));
            for (int i = 0; i < w * (int)with[1]; i++) {
                bool m = Math.Abs(L(with, i) - L(without, i)) > thr;
                if (m) g.FillRectangle(red, (i % w) * cell, (i / w) * cell, cell, cell);
                else if (set.Contains(i)) g.FillRectangle(blue, (i % w) * cell, (i / w) * cell, cell, cell);
            }
            bmp.Save(outPath, ImageFormat.Png);
        }
    }
    static bool InPoly(double[] p, double x, double y) {
        bool c = false; int n = p.Length / 2;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            double xi = p[2 * i], yi = p[2 * i + 1], xj = p[2 * j], yj = p[2 * j + 1];
            if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi + 1e-9) + xi)) c = !c;
        }
        return c;
    }
    // Overlay for humans: the "with" image, mask cells tinted red, polygon cells without contribution tinted blue.
    public static void Overlay(string withPath, float[] with, float[] without, int cell, double[] poly, float thr, string outPath) {
        int w = (int)with[0];
        using (var bmp = new Bitmap(withPath))
        using (var g = Graphics.FromImage(bmp)) {
            var red = new SolidBrush(Color.FromArgb(90, 255, 0, 0)); var blue = new SolidBrush(Color.FromArgb(110, 0, 80, 255));
            for (int y = 0; y < (int)with[1]; y++)
                for (int x = 0; x < w; x++) {
                    bool m = Math.Abs(L(with, y * w + x) - L(without, y * w + x)) > thr;
                    if (m) g.FillRectangle(red, x * cell, y * cell, cell, cell);
                    else if (poly != null && poly.Length >= 6 && InPoly(poly, x + 0.5, y + 0.5)) g.FillRectangle(blue, x * cell, y * cell, cell, cell);
                }
            bmp.Save(outPath, ImageFormat.Png);
        }
    }
    // Pure-black cells: fraction of cells with luma < thr (unlit / missing geometry candidates).
    public static double BlackFraction(float[] rgb, float thr) {
        int n = (int)rgb[0] * (int)rgb[1], k = 0;
        for (int i = 0; i < n; i++) if (L(rgb, i) < thr) k++;
        return (double)k / n;
    }
    // Count pixels that match a saturated debug-overlay colour (unlit GL_LINES: pure green / cyan /
    // yellow / magenta as drawn by World::draw's debug overlay) - tolerance tol per channel.
    public static int DebugPixels(string path, int tol) {
        int[][] cols = { new[] {77, 255, 102}, new[] {51, 204, 255}, new[] {255, 255, 51}, new[] {255, 77, 255} };
        int hits = 0;
        using (var bmp = new Bitmap(path)) {
            var data = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                var row = new byte[data.Stride];
                for (int y = 0; y < bmp.Height; y++) {
                    System.Runtime.InteropServices.Marshal.Copy(data.Scan0 + y * data.Stride, row, 0, data.Stride);
                    for (int x = 0; x < bmp.Width; x++) {
                        int b = row[x * 3], g = row[x * 3 + 1], r = row[x * 3 + 2];
                        foreach (var c in cols)
                            if (Math.Abs(r - c[0]) <= tol && Math.Abs(g - c[1]) <= tol && Math.Abs(b - c[2]) <= tol) { hits++; break; }
                    }
                }
            } finally { bmp.UnlockBits(data); }
        }
        return hits;
    }
}
