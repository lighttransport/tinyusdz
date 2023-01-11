extern crate binary_reader;

use binary_reader::{BinaryReader, Endian};

fn main() {
    let vector: Vec<u8> = vec![
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x00, 0x0B,
        0x77,
    ];

    let mut binary = BinaryReader::from_vec(&vector);
    binary.set_endian(Endian::Big);

    assert_eq!("Hello, World!", binary.read_cstr().unwrap());
    assert_eq!(2_935, binary.read_i16().unwrap());
}
