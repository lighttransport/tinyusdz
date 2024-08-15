let TinyUSDZ = null;

const TINYUSDZ_PATH = './tinyusdz.js';

async function TinyUSDZLoader() {
	if ( TinyUSDZ === null ) {

		const { default: initTinyUSDZ } = await import( TINYUSDZ_PATH );
		TinyUSDZ = await initTinyUSDZ();

	}

  console.log( TinyUSDZ );
}

export { TinyUSDZLoader };
