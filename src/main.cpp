#include <M5Stack.h> //ライブラリのインクルード:

#define WIDTH (M5.Lcd.width() / 8) //8x8の熱画像を表示するための定義
#define HEIGHT (M5.Lcd.height() / 8)

#define PCTL 0x00
#define RST 0x01
#define FPSC 0x02
#define INTC 0x03
#define STAT 0x04
#define SCLR 0x05
#define AVE 0x07
#define INTHL 0x08
#define TTHL 0x0E
#define INT0 0x10
#define T01L 0x80

#define AMG88_ADDR 0x69 //AMG8833センサーモジュールのI2Cアドレス0x69

//色の変換に使用されるためのゲインとオフセット値
float gain = 10.0;          //シグモイド関数 ゲイン
float offset_x = 0.2;       // オフセット
float offset_green = 0.6;   // 緑カーブ用オフセット

//値をシグモイド関数を使用して0から1の範囲に変換します。
//これにより、値が熱画像の色にマッピングされます。
float sigmoid(float x, float g, float o) {
    return (tanh((x + o) * g / 2) + 1) / 2;
}


//0から1の値を受け取り、それをRGBカラーに変換して16ビットのカラーコードを返します。
//このカラーコードは、熱画像の各ピクセルの色を決定します。
uint16_t heat(float x) {  // 0.0〜1.0の値を青から赤の色に変換する
    x = x * 2 - 1;  // -1 <= x < 1 に変換

    float r = sigmoid(x, gain, -1 * offset_x);
    float b = 1.0 - sigmoid(x, gain, offset_x);
    float g = sigmoid(x, gain, offset_green) + (1.0 - sigmoid(x, gain, -1 * offset_green)) - 1;

    return (((int)(r * 255)>>3)<<11) | (((int)(g * 255)>>2)<<5) | ((int)(b * 255)>>3);
}

//I2Cデバイスに対して8ビットのデータを書き込むための関数
//id : I2Cデバイスのアドレス
//reg : 書き込むレジスタのアドレス
//data :レジスタに書き込む8ビットのデータ
void write8(int id, int reg, int data) {
    //指定したI2Cアドレス (id) に対してデータの送信を開始
    Wire.beginTransmission(id); 
    //指定したデータ (reg) をI2Cデバイスに送信
    Wire.write(reg); 
    //指定したデータ (data) をI2Cデバイスに送信
    Wire.write(data);
    //データ送信を終了し、結果を取得
    uint8_t result = Wire.endTransmission();
    // Serial.printf("reg: 0x%02x, result: 0x%02x\r\n", reg, result);
}

//スケッチの初期設定
void setup() {
    //M5Stackデバイスを初期化
    M5.begin();
    //シリアル通信を初期化
    Serial.begin(115200);
    //21番ピンと22番ピンを内部プルアップリスターンに設定
    pinMode(21, INPUT_PULLUP);
    pinMode(22, INPUT_PULLUP);
    //I2C通信の初期化
    Wire.begin();
    
    write8(AMG88_ADDR, FPSC, 0x00);  // センサーデータの取得レートを10fpsに設定
    write8(AMG88_ADDR, INTC, 0x00);  // INT(割り込み制御レジスター)出力無効
    write8(AMG88_ADDR, 0x1F, 0x50);  // 移動平均出力モード有効
    write8(AMG88_ADDR, 0x1F, 0x45);
    write8(AMG88_ADDR, 0x1F, 0x57);
    write8(AMG88_ADDR, AVE, 0x20);   //アベレージレジスター
    write8(AMG88_ADDR, 0x1F, 0x00);
}

//指定したI2Cアドレスから指定したレジスタからデータを読み取り、配列に格納
//id : I2Cデバイスのアドレスを指定します。
//reg : 読み取りを開始するレジスタのアドレスを指定します。
//data : 読み取ったデータを格納する整数の配列へのポインタです。
//datasize : 読み取りたいデータのバイト数を指定します。
void dataread(int id,int reg,int *data,int datasize) {
    //指定したI2Cアドレス (id) に対してデータの送信を開
    Wire.beginTransmission(id);
    //指定したデータ (reg) をI2Cデバイスに送信
    Wire.write(reg);
    //データ送信を終了し、I2C通信を完了
    Wire.endTransmission();
    //指定したI2Cアドレス (id) から指定したバイト数 (datasize) のデータを要求
    Wire.requestFrom(id, datasize);
    int i = 0;
    //デバイスからデータが利用可能かつ読み取り対象のバイト数に達していない限り続ける。
    while (Wire.available() && i < datasize) {
        data[i++] = Wire.read();
    }
}

//センサーデータを読み取り、それを使用して熱画像を生成し、M5Stackデバイスに表示します。
void loop() {
    //センサーデータから生成された熱画像の各ピクセルの温度値を格納するための配列
    float temp[64];
    //I2Cデバイスから読み取ったセンサーデータを格納するための整数の配列
    int sensorData[128];

    dataread(AMG88_ADDR, T01L, sensorData, 128);

    //センサーからのデータを適切に変換して temp 配列に格納
    //センサーデータは16ビットの2の補数形式で提供されるた
    for (int i = 0 ; i < 64 ; i++) {
        int16_t temporaryData = sensorData[i * 2 + 1] * 256 + sensorData[i * 2];
        if(temporaryData > 0x200) {
            temp[i] = (-temporaryData +  0xfff) * -0.25;
        } else {
            temp[i] = temporaryData * 0.25;
        }
    }
    //ディスプレイを黒でクリア
    M5.Lcd.fillScreen(BLACK);

    int x, y;

    //8x8の熱画像を描画
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            float t = temp[(8 - y - 1) * 8 + 8 - x - 1];
            uint16_t color = heat(map(constrain((int)t, 0, 60), 0, 60, 0, 100) / 100.0);
            //各ピクセルを描画
            M5.Lcd.fillRect(x * WIDTH, y * HEIGHT, WIDTH, HEIGHT, color);
            M5.Lcd.setCursor(x * WIDTH + WIDTH / 2, y * HEIGHT + HEIGHT / 2);
            M5.Lcd.setTextColor(BLACK, color);
            M5.Lcd.printf("%d", (int)t);
        }
    }

    //熱画像の表示が0.5秒ごとに更新
    delay(500);
}