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
using System.Runtime.InteropServices;
using System.Diagnostics;

namespace AnalogTVTest {
    public partial class Form1 : Form {

        //private Graphics g;
        private IntPtr tv_ptr;
        //private AnalogTV.analogtv_reception rec;
        private IntPtr rec_ptr;
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
            
            this.tv_ptr = AnalogTV.analogtv_allocate( this.Handle );
            IntPtr input_ptr = AnalogTV.analogtv_input_allocate();
            AnalogTV.analogtv_set_defaults( this.tv_ptr, new StringBuilder( "" ) );
            AnalogTV.analogtv_setup_sync( input_ptr, 1, 0 );
            
            int[] field_ntsc = new int[4] { 0, 0, 0, 0 };
            AnalogTV.analogtv_lcp_to_ntsc( AnalogTV.ANALOGTV_BLACK_LEVEL, 0.0, 0.0, field_ntsc );

            AnalogTV.analogtv_draw_solid(
                input_ptr,
                AnalogTV.ANALOGTV_VIS_START,
                AnalogTV.ANALOGTV_VIS_END,
                AnalogTV.ANALOGTV_TOP,
                AnalogTV.ANALOGTV_BOT,
                field_ntsc
            );

            AnalogTV.analogtv_lcp_to_ntsc( AnalogTV.ANALOGTV_BLACK_LEVEL, 0.0, 0.0, this.ntsc );
            AnalogTV.analogtv_draw_solid( input_ptr, AnalogTV.ANALOGTV_VIS_START, AnalogTV.ANALOGTV_VIS_END, AnalogTV.ANALOGTV_TOP, AnalogTV.ANALOGTV_BOT, this.ntsc );

            this.rec_ptr = AnalogTV.analogtv_reception_allocate( 2.0f, input_ptr );

            Timer TPaint = new Timer();
            TPaint.Interval = 1;
            TPaint.Tick += this.TPaint_Tick;
            TPaint.Enabled = true;
            TPaint.Start();
        }

        private void TPaint_Tick( Object sender, EventArgs e ) {
            AnalogTV.analogtv_reception_update( this.rec_ptr );

            AnalogTV.analogtv_draw( this.tv_ptr, 0.04, ref this.rec_ptr, 1 );
        }
    }
}
