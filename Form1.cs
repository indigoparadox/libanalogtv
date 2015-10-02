using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using libAnalogTV.Interop;

namespace AnalogTVTest {
    public partial class Form1 : Form {

        private Graphics g;
        private IntPtr tv;
        private IntPtr input;
        private int[][] pixels;
        private int[] ntsc;

        public Form1() {
            InitializeComponent();
        }

        private void Form1_Load( object sender, EventArgs e ) {
            this.ntsc = new int[4] { 0, 0, 0, 0 };
            this.pixels = new int[this.Width][];
            for( int i = 0 ; this.Height > i ; i++ ) {
                this.pixels[i] = new int[this.Height];
            }

            this.g = Graphics.FromHwnd( this.Handle );
            this.tv = AnalogTV.analogtv_allocate( this.g.GetHdc(), this.Handle );
            this.input = AnalogTV.analogtv_input_allocate();
            AnalogTV.analogtv_setup_sync( this.input, 1, 0 );

            AnalogTV.analogtv_lcp_to_ntsc( AnalogTV.ANALOGTV_BLACK_LEVEL, 0.0, 0.0, this.ntsc );
            AnalogTV.analogtv_draw_solid( this.input, AnalogTV.ANALOGTV_VIS_START, AnalogTV.ANALOGTV_VIS_END, AnalogTV.ANALOGTV_TOP, AnalogTV.ANALOGTV_BOT, this.ntsc );


        }

        private void Form1_Paint( object sender, PaintEventArgs e ) {
            AnalogTV.analogtv_draw( this.tv, 7.0, IntPtr.Zero, 0 );
        }
    }
}
