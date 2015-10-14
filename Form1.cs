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

        private static readonly int PONG_WIDTH = 10;
        private static readonly int PONG_HEIGHT = 10;
        private static readonly int PONG_MAX_X = 240;
        private static readonly int PONG_MAX_Y = 160;

        //private Graphics g;
        private AnalogTV.analogtv tvPtr;
        //private AnalogTV.analogtv_reception rec;
        private AnalogTV.analogtv_reception recPtr;
        private int[][] pixels;
        private int[] ntsc;
        private AnalogTV.analogtv_input inputPtr;

        private int pongX = 0;
        private int pongY = 0;
        private int pongXInc = 30;
        private int pongYInc = 30;

        public Form1() {
            InitializeComponent();
        }

        private void DrawPong(int x, int y) {
            int[] field_ntsc = new int[4] { 0, 0, 0, 0 };

            AnalogTV.analogtv_lcp_to_ntsc( AnalogTV.ANALOGTV_BLACK_LEVEL, 0.0, 0.0, field_ntsc );

            AnalogTV.analogtv_draw_solid( this.inputPtr,
                AnalogTV.ANALOGTV_VIS_START, AnalogTV.ANALOGTV_VIS_END,
                AnalogTV.ANALOGTV_TOP, AnalogTV.ANALOGTV_BOT,
                field_ntsc );

            AnalogTV.analogtv_color( 1, field_ntsc );

            //AnalogTV.analogtv_draw_solid( this.inputPtr,
            AnalogTV.analogtv_draw_solid( this.inputPtr,
                AnalogTV.ANALOGTV_VIS_START + x, AnalogTV.ANALOGTV_VIS_START + x + PONG_WIDTH,
                AnalogTV.ANALOGTV_TOP + y, AnalogTV.ANALOGTV_TOP + y + PONG_HEIGHT,
                field_ntsc );

            //AnalogTV.analogtv_reception_reallocate( this.recPtr, this.inputPtr );
        }

        private void Form1_Load( object sender, EventArgs e ) {
            this.ntsc = new int[4] { 0, 0, 0, 0 };
            this.pixels = new int[this.Width][];
            for( int i = 0 ; this.Height > i ; i++ ) {
                this.pixels[i] = new int[this.Height];
            }
            
            this.tvPtr = Marshal.PtrToStructure<AnalogTV.analogtv>( AnalogTV.analogtv_allocate(this.Handle) );
            this.inputPtr = Marshal.PtrToStructure<AnalogTV.analogtv_input>( AnalogTV.analogtv_input_allocate() );
            //AnalogTV.analogtv_set_defaults( this.tvPtr );
            AnalogTV.analogtv_setup_sync( inputPtr, 1, 0 );
            
            //int[] field_ntsc = new int[4] { 0, 0, 0, 0 };

            /*
            AnalogTV.analogtv_lcp_to_ntsc( AnalogTV.ANALOGTV_BLACK_LEVEL, 0.0, 0.0, field_ntsc );

            AnalogTV.analogtv_draw_solid(
                inputPtr,
                AnalogTV.ANALOGTV_VIS_START,
                AnalogTV.ANALOGTV_VIS_END,
                AnalogTV.ANALOGTV_TOP,
                AnalogTV.ANALOGTV_BOT,
                field_ntsc
            );

            AnalogTV.analogtv_lcp_to_ntsc( AnalogTV.ANALOGTV_BLACK_LEVEL, 0.0, 0.0, this.ntsc );
            AnalogTV.analogtv_draw_solid( inputPtr, AnalogTV.ANALOGTV_VIS_START, AnalogTV.ANALOGTV_VIS_END, AnalogTV.ANALOGTV_TOP, AnalogTV.ANALOGTV_BOT, this.ntsc );
            */

            this.recPtr = AnalogTV.analogtv_reception_allocate( 2.0f, inputPtr );

            Timer TPaint = new Timer();
            TPaint.Interval = 50;
            TPaint.Tick += this.TPaint_Tick;
            TPaint.Enabled = true;
            TPaint.Start();

            Timer TPong = new Timer();
            TPong.Interval = 100;
            TPong.Tick += this.TPong_Tick;
            TPong.Enabled = true;
            TPong.Start();
        }

        private void TPaint_Tick( Object sender, EventArgs e ) {
            AnalogTV.analogtv_reception_update( this.recPtr );

            AnalogTV.analogtv_draw( this.tvPtr, 0.04, ref this.recPtr, 1 );
        }

        private void TPong_Tick( Object sender, EventArgs e ) {
            if( PONG_MAX_X <= this.pongX || 0 >= this.pongX ) {
                this.pongXInc *= -1;
            }
            if( PONG_MAX_Y <= this.pongY || 0 >= this.pongY ) {
                this.pongYInc *= -1;
            }

            this.pongX += this.pongXInc;
            this.pongY += this.pongYInc;

            this.DrawPong( this.pongX, this.pongY );
        }

        private void Form1_Resize( object sender, EventArgs e ) {
            this.Width = (this.Width / 12) * 12;
            AnalogTV.analogtv_reconfigure( this.tvPtr );
        }
    }
}
