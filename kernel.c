#define VIDEO_MEMORY 0xb8000
#define MAX_COLS 80
#define MAX_ROWS 25
void kprint_at(char *message, int row, int col);
void main(){
    kprint_at("This is a XYTriste kernel", 0, 0);
    kprint_at("It works!!!", 1, 0);

    while(1);
}
void kprint_at(char *message, int row, int col){
    char *video_memory = (char *) VIDEO_MEMORY;
    
    int offset = (row * MAX_COLS + col) * 2;
    int i;
    for(i = 0; message[i] != '\0'; i++){
        *video_memory = 'A';
        int actually_offset = offset + i * 2;
        video_memory[actually_offset] = message[i];
        video_memory[actually_offset + 1] = 0x4f;
    }
}
