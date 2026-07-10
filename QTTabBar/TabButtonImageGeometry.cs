using System;
using System.Drawing;

namespace QTTabBarLib {
    internal static class TabButtonImageGeometry {
        internal const int CloseButtonStateCount = 4;

        internal static Rectangle[] GetCloseButtonFrames(Size imageSize) {
            if(imageSize.Width <= 0 || imageSize.Height <= 0) {
                return new Rectangle[0];
            }

            if(imageSize.Width % CloseButtonStateCount == 0 &&
                    imageSize.Width >= imageSize.Height * CloseButtonStateCount) {
                int width = imageSize.Width / CloseButtonStateCount;
                Rectangle[] frames = new Rectangle[CloseButtonStateCount];
                for(int i = 0; i < frames.Length; i++) {
                    frames[i] = new Rectangle(i * width, 0, width, imageSize.Height);
                }
                return frames;
            }

            if(imageSize.Height % CloseButtonStateCount == 0 &&
                    imageSize.Height >= imageSize.Width * CloseButtonStateCount) {
                int height = imageSize.Height / CloseButtonStateCount;
                Rectangle[] frames = new Rectangle[CloseButtonStateCount];
                for(int i = 0; i < frames.Length; i++) {
                    frames[i] = new Rectangle(0, i * height, imageSize.Width, height);
                }
                return frames;
            }

            return new[] { new Rectangle(Point.Empty, imageSize) };
        }

        internal static Size FitWithin(Size imageSize, Size maximumSize) {
            if(imageSize.Width <= 0 || imageSize.Height <= 0 ||
                    maximumSize.Width <= 0 || maximumSize.Height <= 0) {
                return Size.Empty;
            }

            float scale = Math.Min(1f, Math.Min(
                    (float)maximumSize.Width / imageSize.Width,
                    (float)maximumSize.Height / imageSize.Height));
            return new Size(
                    Math.Max(1, (int)Math.Round(imageSize.Width * scale)),
                    Math.Max(1, (int)Math.Round(imageSize.Height * scale)));
        }

        internal static Rectangle CenterWithin(Rectangle bounds, Size imageSize) {
            return new Rectangle(
                    bounds.X + (bounds.Width - imageSize.Width) / 2,
                    bounds.Y + (bounds.Height - imageSize.Height) / 2,
                    imageSize.Width,
                    imageSize.Height);
        }
    }
}
