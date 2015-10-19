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
    public partial class PongViewer : Form {

        //private static readonly int PONG_WIDTH = 30;
        //private static readonly int PONG_HEIGHT = 30;
        //private static readonly int PONG_MAX_X;
        //private static readonly int PONG_MAX_Y;

        //private Graphics g;
        //private AnalogTV.analogtv tvPtr;
        private IntPtr tvPtr;
        //private AnalogTV.analogtv_reception rec;
        private IntPtr recPtr;
        //private AnalogTV.analogtv_input inputPtr;
        private IntPtr inputPtr;
        private IntPtr pongBitmap = IntPtr.Zero;

        private int pongX = 0;
        private int pongY = 0;
        private int pongXInc = 10;
        private int pongYInc = 10;

        private uint pongWidth = 10;
        private uint pongHeight = 10;
        private uint pongMaxX = AnalogTV.ANALOGTV_VIS_LEN - 10;
        private uint pongMaxY = AnalogTV.ANALOGTV_VISLINES - 10;

        public PongViewer() {
            InitializeComponent();
        }

        private void DrawPong(int x, int y) {
            int[] field_ntsc = new int[4] { 0, 0, 0, 0 };

            AnalogTV.analogtv_lcp_to_ntsc( AnalogTV.ANALOGTV_WHITE_LEVEL, 0.0, 0.0, field_ntsc );

            AnalogTV.analogtv_draw_solid( this.inputPtr,
                AnalogTV.ANALOGTV_VIS_START, AnalogTV.ANALOGTV_VIS_END,
                AnalogTV.ANALOGTV_TOP, AnalogTV.ANALOGTV_BOT,
                field_ntsc );

            AnalogTV.analogtv_color( 5, field_ntsc );

            if( IntPtr.Zero == this.pongBitmap) {
                AnalogTV.analogtv_draw_solid( this.inputPtr,
                    AnalogTV.ANALOGTV_VIS_START + x, (int)(AnalogTV.ANALOGTV_VIS_START + x + this.pongWidth),
                    AnalogTV.ANALOGTV_TOP + y, (int)(AnalogTV.ANALOGTV_TOP + y + this.pongHeight),
                    field_ntsc );
            } else {
                AnalogTV.analogtv_draw_image( this.inputPtr,
                    this.pongBitmap, AnalogTV.ANALOGTV_VIS_START + x, AnalogTV.ANALOGTV_TOP + y,
                    this.pongWidth, this.pongHeight );
            }

            //AnalogTV.analogtv_reception_reallocate( this.recPtr, this.inputPtr );
        }

        public void SetPongImage( string pathIn ) {
            this.pongX = 0;
            this.pongY = 0;
            uint widthIn = 0, heightIn = 0;
            AnalogTV.analogtv_load_bitmap( pathIn, out this.pongBitmap, out widthIn, out heightIn );
            this.pongWidth = widthIn;
            this.pongHeight = heightIn;
            this.pongMaxX = AnalogTV.ANALOGTV_VIS_LEN - widthIn;
            this.pongMaxY = AnalogTV.ANALOGTV_VISLINES - heightIn;
        }

        private void PongViewer_Load( object sender, EventArgs e ) {

            //this.Width = 640;
            //this.Height = 480;

            int[] ntsc = new int[4] { 0, 0, 0, 0 };
            /*
            this.pixels = new int[this.Width][];
            for( int i = 0 ; this.Height > i ; i++ ) {
                this.pixels[i] = new int[this.Height];
            }
            */

            //this.tvPtr = (AnalogTV.analogtv)Marshal.PtrToStructure( AnalogTV.analogtv_allocate(this.Handle),typeof(AnalogTV.analogtv) );
            //this.inputPtr = (AnalogTV.analogtv_input)Marshal.PtrToStructure( AnalogTV.analogtv_input_allocate(),typeof(AnalogTV.analogtv_input) );
            this.tvPtr = AnalogTV.analogtv_allocate( panel1.Handle );
            this.inputPtr = AnalogTV.analogtv_input_allocate();
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

            /*
            Timer TPaint = new Timer();
            TPaint.Interval = 50;
            TPaint.Tick += this.TPaint_Tick;
            TPaint.Enabled = true;
            TPaint.Start();
            */

            Timer TPong = new Timer();
            TPong.Interval = 50;
            TPong.Tick += this.TPong_Tick;
            TPong.Enabled = true;
            TPong.Start();
        }

        private void TPong_Tick( Object sender, EventArgs e ) {
            if( this.pongMaxX < this.pongX || 0 > this.pongX ) {
                this.pongXInc *= -1;
            }
            if( this.pongMaxY < this.pongY || 0 > this.pongY ) {
                this.pongYInc *= -1;
            }

            this.pongX += this.pongXInc;
            this.pongY += this.pongYInc;

            this.DrawPong( this.pongX, this.pongY );

            AnalogTV.analogtv_reception_update( this.recPtr );

            AnalogTV.analogtv_draw( this.tvPtr, 0.04, ref this.recPtr, 1 );
        }

        private void PongViewer_Resize( object sender, EventArgs e ) {
            if( IntPtr.Zero != this.tvPtr ) {
                AnalogTV.analogtv_reconfigure( this.tvPtr );
            }
        }

        private void exitToolStripMenuItem_Click( object sender, EventArgs e ) {
            Application.Exit();
        }

        private void openToolStripMenuItem_Click( object sender, EventArgs e ) {
            OpenFileDialog openDialog = new OpenFileDialog();
            openDialog.Filter = "Bitmap File|*.bmp";
            DialogResult openResult = openDialog.ShowDialog();
            if( DialogResult.OK == openResult ) {
                this.SetPongImage( openDialog.FileName );
            }
        }
    }
}
