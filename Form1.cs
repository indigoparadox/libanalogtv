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

namespace AnalogTVTest {
    public partial class Form1 : Form {

        //private Graphics g;
        private IntPtr tv;
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

            AnalogTV.analogtv_reception rec = new AnalogTV.analogtv_reception();
            //this.rec_ptr = 
            this.tv = AnalogTV.analogtv_allocate( this.Handle );
            rec.input = AnalogTV.analogtv_input_allocate();
            AnalogTV.analogtv_set_defaults( this.tv, new StringBuilder( "" ) );
            AnalogTV.analogtv_setup_sync( rec.input, 1, 0 );
            
            rec.level = 2.0;
            rec.ofs = 0;
            rec.multipath = 0.0;

            int[] field_ntsc = new int[4] { 0, 0, 0, 0 };
            AnalogTV.analogtv_lcp_to_ntsc( AnalogTV.ANALOGTV_BLACK_LEVEL, 0.0, 0.0, field_ntsc );

            AnalogTV.analogtv_draw_solid(
                rec.input,
                AnalogTV.ANALOGTV_VIS_START,
                AnalogTV.ANALOGTV_VIS_END,
                AnalogTV.ANALOGTV_TOP,
                AnalogTV.ANALOGTV_BOT,
                field_ntsc
            );

            AnalogTV.analogtv_lcp_to_ntsc( AnalogTV.ANALOGTV_BLACK_LEVEL, 0.0, 0.0, this.ntsc );
            AnalogTV.analogtv_draw_solid( rec.input, AnalogTV.ANALOGTV_VIS_START, AnalogTV.ANALOGTV_VIS_END, AnalogTV.ANALOGTV_TOP, AnalogTV.ANALOGTV_BOT, this.ntsc );


            this.rec_ptr = Marshal.AllocHGlobal( Marshal.SizeOf( rec ) );
            Marshal.StructureToPtr( rec, this.rec_ptr, false );

            Timer TPaint = new Timer();
            TPaint.Interval = 100;
            TPaint.Tick += this.TPaint_Tick;
            TPaint.Enabled = true;
            TPaint.Start();
        }

        private void TPaint_Tick( Object sender, EventArgs e ) {
            //using( Graphics g = this.CreateGraphics() ) {
            AnalogTV.analogtv_reception_update( this.rec_ptr );

            //g.FillRectangle( Brushes.Blue, 0, 0, 100, 100 );

            //AnalogTV.analogtv fr = (AnalogTV.analogtv)Marshal.PtrToStructure( this.tv, typeof( AnalogTV.analogtv) );
            
            AnalogTV.analogtv_draw( this.tv, 0.04, this.rec_ptr, 1 );
            //}
        }
    }
}
