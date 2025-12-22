// 1. 计算字符串长度
int strlen(char s[]){
    int len = 0;
    while(s[len] != '\0'){
        len++;
    }
    return len;
}

// 2. 反转字符串（用于实现字符串与整数转换）
void reverse(char s[]){
    int c, i, j;
    for (i = 0, j = strlen(s) - 1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

// 3. 字符串比较函数
int strcmp(char s1[], char s2[]){
    int i;
    for(i = 0; s1[i] == s2[i]; i++){
        if(s1[i] == '\0'){
            return 0;
        }
    }
    return s1[i] - s2[i];
}

// 4. 在字符串末尾追加一个字符
void append(char s[], char n){
    int len = strlen(s);
    s[len] = n;
    s[len + 1] = '\0';
}

// 5. 退格键删除字符串
void backspace(char s[]){
    int len = strlen(s);
    if(len > 0){
       s[len - 1] = '\0'; 
    }
    
}