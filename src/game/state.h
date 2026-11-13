/** ****************************************************************************
 *
 * \file	state.h
 *
 * \brief	DS offsets of the game state and of the tables loaded from
 * 			hot_data.
 *
 * \details	The player's state (money, cars, parts, the classified ads)
 * 			stays in the data segment image of exe.c instead of becoming
 * 			named variables: its records link to each other with near
 * 			pointers (DS offsets) and the save game functions read and
 * 			write it as blocks of memory. Read and write it with
 * 			exe_readS16, exe_writeU16, exe_readU32, exe_writeU32 and
 * 			exe_data.
 *
 ******************************************************************************/

#ifndef GAME_STATE_H_INCLUDED
#define GAME_STATE_H_INCLUDED

/*******************************************************************************
;
;	I N C L U D E S
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	D E F I N E S
;
;-----------------------------------------------------------------------------*/

#define STATE_MONEY			 0x7eaa /**< u32: the player's money. */
#define STATE_UNKNOWN_AE	 0x7eae /**< s16: 10 in a new game; unknown. */
#define STATE_CAR			 0x7eb0 /**< Near pointer: the car in use, or 0. */
#define STATE_STORED_CARS	 0x7eb2 /**< Near pointer: stored cars, or 0. */
#define STATE_FREE_CARS		 0x7eb4 /**< Near pointer: unused car records. */
#define STATE_CAR_COUNT		 0x7eb6 /**< s16: car records in use (max 16). */
#define STATE_SPARE_PARTS	 0x7eb8 /**< Near pointer: unfitted parts, or 0. */
#define STATE_FREE_PARTS	 0x7eba /**< Near pointer: unused part records. */
#define STATE_PART_COUNT	 0x7ebc /**< s16: part records in use (max 140). */
#define STATE_PLAYER_NAME	 0x7e9a /**< char[16]: name typed by the player. */

#define STATE_CAR_RECORDS	 0x83b0 /**< 16 car records. */
#define STATE_CAR_SIZE		 0x28	/**< Bytes of a car record. */
#define STATE_CAR_MAX		 16		/**< Car records. */
#define STATE_CAR_PRICE		 0		/**< Car record: s16, price paid. */
#define STATE_CAR_MODEL		 2		/**< Car record: s16 model, -1 unused. */
#define STATE_CAR_PAINT		 4		/**< Car record: u8 paint, 0..5. */
#define STATE_CAR_CLASS		 5		/**< Car record: u8 class of the model. */
#define STATE_CAR_GEARBOX	 6		/**< Car record: part, table 9.. */
#define STATE_CAR_ENGINE	 8		/**< Car record: part, table 0.. */
#define STATE_CAR_INTAKE	 10		/**< Car record: part, table 0x19.. */
#define STATE_CAR_CARBS		 12		/**< Car record: 3 parts, table 0x16.. */
#define STATE_CAR_TIRES		 18		/**< Car record: part, table 0x28.. */
#define STATE_CAR_WEAR		 0x14	/**< Car record: 5 bytes, 3 when new. */
#define STATE_CAR_CARB_WEAR	 0x19	/**< Car record: 2 bytes per carb. */
#define STATE_CAR_UNKNOWN_1F 0x1f	/**< Car record: u8, 1 when made. */
#define STATE_CAR_UNKNOWN_20 0x20	/**< Car record: s8, random -8..4. */
#define STATE_CAR_FUEL		 0x22	/**< Car record: s16 gas, 0x28 when made. */
#define STATE_CAR_UNKNOWN_24 0x24	/**< Car record: u8, 0 when made. */
#define STATE_CAR_FLAGS		 0x25	/**< Car record: u8 sticker & body mods. */
#define STATE_CAR_NEXT		 0x26	/**< Car record: near pointer to next. */
#define STATE_PART_RECORDS	 0x866a /**< 140 part records. */
#define STATE_PART_SIZE		 8		/**< Bytes of a part record. */
#define STATE_PART_MAX		 140	/**< Part records. */
#define STATE_PART_PRICE	 0		/**< Part record: s16, price. */
#define STATE_PART_KIND		 2		/**< Part record: word of its kind. */
#define STATE_PART_VALUE	 4		/**< Part record: s16 index, -1 unused. */
#define STATE_PART_NEXT		 6		/**< Part record: near pointer to next. */

/*
 * Classified ads: 18-byte records in hot_data block 0 (DS:0x7ff8); the
 * fields below count from the record's start.
 */
#define STATE_ADS		  0x7ff8 /**< The ad records. */
#define STATE_AD_SIZE	  18	 /**< Bytes of an ad record. */
#define STATE_AD_PRICE	  6		 /**< s16: set to 0x629 for the demo's ad. */
#define STATE_AD_CAR	  8		 /**< s16: index of the car model. */
#define STATE_AD_COLOR	  10	 /**< u8: random 0..5. */
#define STATE_AD_CLEARED  11	 /**< 6 bytes cleared for a new ad. */
#define STATE_AD_FLAGS	  14	 /**< u8: bits 0..1, 2, 3; meaning unknown. */
#define STATE_AD_WEAR	  15	 /**< u8: random 0..8, or 0; unknown. */
#define STATE_ADS_PER_SET 7		 /**< Ads of each of the 3 price classes. */
#define STATE_ADS_DONE	  0x817d /**< u8: cleared when ads are made. */
#define STATE_DEMO_AD	  0x4ff4 /**< s16: index of the demo's ad. */

/*
 * Car models: 10-byte records in hot_data block 1 (DS:0x7d86), and a
 * second 10-byte record per model in block 2 (DS:0x8df0).
 */
#define STATE_MODELS		 0x7d86 /**< The model records. */
#define STATE_MODEL_SIZE	 10		/**< Bytes of a model record. */
#define STATE_MODEL_COUNT	 25		/**< Models that can be advertised. */
#define STATE_MODEL_PRICE	 0		/**< u16: price in the newspaper. */
#define STATE_MODEL_CLASS	 2		/**< u8: < 3 cheap, 3..5 mid, else top. */
#define STATE_MODEL_NEXT	 3		/**< s8: next model in the ads, or -1. */
#define STATE_MODEL_FITTED	 4		/**< u16: the parts it comes with. */
#define STATE_MODEL_TEXT	 6		/**< u16: its ad, offset from DS:0x2c02. */
#define STATE_MODEL_BODY	 8		/**< u16: resource of the car body. */
#define STATE_FIRST_MODEL	 0x7d16 /**< s16: first model in the ads. */
#define STATE_MODEL_KIND	 5		/**< u8: low 3 bits == 2 sets ad flag 8. */
#define STATE_MODEL_PARTS	 0x8df0 /**< Second record of each model. */
#define STATE_PARTS_HOOD	 0		/**< s16: open hood resource. */
#define STATE_PARTS_ROOF	 2		/**< s16: chopped roof resource, or -1. */
#define STATE_PARTS_BLOWER	 4		/**< s16: blower resource. */
#define STATE_PARTS_REAR	 6		/**< s16: rear without bumper, or -1. */
#define STATE_PARTS_FRONT	 8		/**< s16: front without bumper, or -1. */
#define STATE_MODEL_HOTSPOTS 0x70e4 /**< 20 bytes/model: 5 hotspot boxes. */
#define STATE_MODEL_OFFSETS	 0x7682 /**< 12 bytes/model: part positions. */
#define STATE_STICKERS		 0x784a /**< 8 x 6-byte sticker records (1..8). */
#define STATE_MODEL_POS		 0x818c /**< 6 bytes/model: window and exhaust. */
#define STATE_TIRE_SPRITES	 0x5030 /**< 4 x 8-byte tire sprite tables. */

/*
 * Parts for sale: 8-byte records at DS:0x4806.
 */
#define STATE_PARTS			0x4806 /**< The parts table. */
#define STATE_PARTS_COUNT	0x2c   /**< Entries of the parts table. */
#define STATE_PARTS_PRICE	0	   /**< u16: price. */
#define STATE_PARTS_SUBTYPE 2	   /**< s8: engine or tire subtype. */
#define STATE_PARTS_KIND	4	   /**< s8: kind of part, 0..4. */
#define STATE_PARTS_TEXT	6	   /**< u16: its ad, offset from DS:0x2c02. */
#define STATE_PART_KINDS	0x4966 /**< 5 words: kind word per kind. */

/*
 * Drawn car and garage car state in DS.
 */
#define STATE_CAR_MODIFIED	  0x825a /**< s16: 1 when body was changed. */
#define STATE_CAR_LEFT		  0x825e /**< s16: left edge of drawn car. */
#define STATE_CAR_TOP		  0x8260 /**< s16: top edge of drawn car. */
#define STATE_CAR_HEIGHT	  0x8262 /**< s16: visible rows of drawn car. */
#define STATE_CAR_HAS_EXHAUST 0x8264 /**< s16: 1 when exhaust pos is set. */
#define STATE_CAR_EXHAUST2_X  0x8266 /**< s16: second exhaust X. */
#define STATE_CAR_EXHAUST2_Y  0x8268 /**< s16: second exhaust Y. */
#define STATE_CAR_EXHAUST1_X  0x826a /**< s16: first exhaust X. */
#define STATE_CAR_EXHAUST1_Y  0x826c /**< s16: first exhaust Y. */
#define STATE_CAR_WINDOW_X	  0x826e /**< s16: driver window X. */
#define STATE_CAR_WINDOW_Y	  0x8270 /**< s16: driver window Y. */
#define STATE_CAR_DOOR_X	  0x8272 /**< s16: byte-aligned door X. */
#define STATE_CAR_DOOR_Y	  0x8274 /**< s16: door Y. */
#define STATE_CAR_DOOR_SHIFT  0x8276 /**< s16: door X bit offset (0..7). */
#define STATE_GARAGE_BODY	  0x827a /**< u16: body resource of garage car. */
#define STATE_GARAGE_PAINT	  0x827c /**< s16: paint of garage car. */
#define STATE_GARAGE_TIRES	  0x827e /**< s16: tire subtype of garage car. */
#define STATE_GARAGE_HOOD	  0x8280 /**< u16: open hood resource, or 0. */
#define STATE_GARAGE_MODEL	  0x8282 /**< s16: model of garage car. */
#define STATE_GARAGE_MODS	  0x8284 /**< 6 s16s: sticker and body mods. */
#define STATE_GARAGE_PARTS	  0x8290 /**< 5 s16s: fitted part indices. */
#define STATE_CAR_SCROLL_X	  0x8b7c /**< s16: last scroll X of the car. */

#define STATE_UNKNOWN_4970	  0x4970 /**< 4 words cleared in a new game. */
#define STATE_STATUS_TITLE	  0x49dc /**< Near pointer: "Bankroll: $". */

/*******************************************************************************
;
;	T Y P E S
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	D A T A
;
;-----------------------------------------------------------------------------*/

/*******************************************************************************
;
;	F U N C T I O N S
;
;-----------------------------------------------------------------------------*/

#endif // GAME_STATE_H_INCLUDED
