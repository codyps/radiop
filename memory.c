void memory_init(struct memory *m)
{
	*m = (struct memory){
		.range_ct = 0,
		.ranges = NULL
	};
}

void memory_insert(struct memory *m, char *data, size_t data_len, uint64_t offset)
{
	/* i need a fast insert mechanism */
}

