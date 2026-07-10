using System;
using System.Drawing;
using System.Windows.Forms;

namespace QTTabBarLib {
    internal static class TabSkinGeometry {
        internal static int GetTabAdvance(int tabWidth, int overlapPixels) {
            return Math.Max(1, tabWidth - overlapPixels);
        }

        internal static Rectangle ApplyContentMargins(Rectangle rectangle, Padding margin) {
            int left = Math.Min(margin.Left, Math.Max(0, rectangle.Width - 1));
            int right = Math.Min(margin.Right, Math.Max(0, rectangle.Width - left - 1));
            int top = Math.Min(margin.Top, Math.Max(0, rectangle.Height - 1));
            int bottom = Math.Min(margin.Bottom, Math.Max(0, rectangle.Height - top - 1));
            return new Rectangle(
                    rectangle.X + left,
                    rectangle.Y + top,
                    Math.Max(1, rectangle.Width - left - right),
                    Math.Max(1, rectangle.Height - top - bottom));
        }

        internal static int MapNineSliceCoordinate(int coordinate, int destinationLength,
                int sourceLength, int leadingMargin, int trailingMargin) {
            if(sourceLength <= 1 || destinationLength <= 1) {
                return 0;
            }
            coordinate = Math.Max(0, Math.Min(coordinate, destinationLength - 1));
            int leading = Math.Max(0, Math.Min(leadingMargin,
                    Math.Min(destinationLength - 1, sourceLength - 1)));
            int trailing = Math.Max(0, Math.Min(trailingMargin,
                    Math.Min(destinationLength - leading - 1, sourceLength - leading - 1)));
            if(coordinate < leading) {
                return coordinate;
            }
            if(coordinate >= destinationLength - trailing) {
                return sourceLength - (destinationLength - coordinate);
            }

            int destinationCenter = destinationLength - leading - trailing;
            int sourceCenter = sourceLength - leading - trailing;
            if(destinationCenter <= 0 || sourceCenter <= 0) {
                return Math.Min(leading, sourceLength - 1);
            }
            int mapped = leading + (int)((long)(coordinate - leading) * sourceCenter / destinationCenter);
            return Math.Max(0, Math.Min(mapped, sourceLength - 1));
        }
    }
}
