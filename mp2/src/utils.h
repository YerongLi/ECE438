struct segment {
  public:
    /**
     * sequence number and length are set on initiliation 
     */
    const long seq_num;
    const long length;
    char* data;
    bool end;
    segment(long seq_num, long length, char* data): 
      seq_num(seq_num), length(length), data(data)  {
    }
} ;
