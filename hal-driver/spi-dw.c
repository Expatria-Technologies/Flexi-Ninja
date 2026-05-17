#include "spi-dw.h"

static inline uint32_t dw_spi_tx_max(struct dw_spi *dws)
{
  uint32_t tx_room, rxtx_gap;

  tx_room = dws->fifo_len - dw_readl(dws, DW_SPI_TXFLR);

  rxtx_gap = dws->fifo_len - (dws->rx_len - dws->tx_len);

  return MIN3((uint32_t)dws->tx_len, tx_room, rxtx_gap);
}

static inline uint32_t dw_spi_rx_max(struct dw_spi *dws)
{
  return MIN(dws->rx_len, dw_readl(dws, DW_SPI_RXFLR));
}

static void dw_writer(struct dw_spi *dws)
{
  uint32_t max = dw_spi_tx_max(dws);
  uint32_t txw = 0;

  while (max--) {
    if (dws->tx) {
      if (dws->n_bytes == 1)
        txw = *(uint8_t *)(dws->tx);
      else if (dws->n_bytes == 2)
        txw = *(uint16_t *)(dws->tx);
      else
        txw = *(uint32_t *)(dws->tx);

      dws->tx += dws->n_bytes;
    }
    dw_writel(dws, DW_SPI_DR, txw);
    --dws->tx_len;
  }
}

static void dw_reader(struct dw_spi *dws)
{
  uint32_t max = dw_spi_rx_max(dws);
  uint32_t rxw;

  while (max--) {
    rxw = dw_readl(dws, DW_SPI_DR);
    if (dws->rx) {
      if (dws->n_bytes == 1)
        *(uint8_t *)(dws->rx) = rxw;
      else if (dws->n_bytes == 2)
        *(uint16_t *)(dws->rx) = rxw;
      else
        *(uint32_t *)(dws->rx) = rxw;

      dws->rx += dws->n_bytes;
    }
    --dws->rx_len;
  }
}

uint32_t dw_spi_prepare_cr0(struct dw_spi *dws, struct spi_device *spi)
{
	uint32_t cr0 = 0;

	if (dw_spi_ip_is(dws, PSSI)) {
		cr0 |= FIELD_PREP(DW_PSSI_CTRLR0_FRF_MASK, DW_SPI_CTRLR0_FRF_MOTO_SPI);

		if (spi->mode & SPI_CPOL)
			cr0 |= DW_PSSI_CTRLR0_SCPOL;
		if (spi->mode & SPI_CPHA)
			cr0 |= DW_PSSI_CTRLR0_SCPHA;

		if (spi->mode & SPI_LOOP)
			cr0 |= DW_PSSI_CTRLR0_SRL;
	} else {
		cr0 |= FIELD_PREP(DW_HSSI_CTRLR0_FRF_MASK, DW_SPI_CTRLR0_FRF_MOTO_SPI);

		if (spi->mode & SPI_CPOL)
			cr0 |= DW_HSSI_CTRLR0_SCPOL;
		if (spi->mode & SPI_CPHA)
			cr0 |= DW_HSSI_CTRLR0_SCPHA;

		if (spi->mode & SPI_LOOP)
			cr0 |= DW_HSSI_CTRLR0_SRL;

		if (dw_spi_ver_is_ge(dws, HSSI, 102A))
			cr0 |= DW_HSSI_CTRLR0_MST;
	}

	return cr0;
}

void dw_spi_update_config(struct dw_spi *dws, struct spi_device *spi, struct dw_spi_cfg *cfg)
{
	uint32_t cr0 = spi->cr0;
	uint32_t speed_hz;
	uint16_t clk_div;

	cr0 |= (cfg->dfs - 1) << dws->dfs_offset;

	if (dw_spi_ip_is(dws, PSSI))
		cr0 |= FIELD_PREP(DW_PSSI_CTRLR0_TMOD_MASK, cfg->tmode);
	else
		cr0 |= FIELD_PREP(DW_HSSI_CTRLR0_TMOD_MASK, cfg->tmode);

	dw_writel(dws, DW_SPI_CTRLR0, cr0);

	if (cfg->tmode == DW_SPI_CTRLR0_TMOD_EPROMREAD ||
	    cfg->tmode == DW_SPI_CTRLR0_TMOD_RO)
		dw_writel(dws, DW_SPI_CTRLR1, cfg->ndf ? cfg->ndf - 1 : 0);

	clk_div = (DIV_ROUND_UP(dws->max_freq, cfg->freq) + 1) & 0xfffe;
	speed_hz = dws->max_freq / clk_div;

	if (dws->current_freq != speed_hz) {
		dw_spi_set_clk(dws, clk_div);
		dws->current_freq = speed_hz;
	}
}

int dw_spi_poll_transfer(struct dw_spi *dws)
{
	uint32_t timeout = 1000000;

	do {
		dw_writer(dws);
		dw_reader(dws);

		if (dws->rx_len && !--timeout) {
			uint32_t sr = dw_readl(dws, DW_SPI_SR);
			DEBUG_PRINT("SPI transfer timeout! rx_len=%u tx_len=%u\n",
				   dws->rx_len, dws->tx_len);
			DEBUG_PRINT("  SSIENR=%08x SR=%08x (BUSY=%d TF_NOT_FULL=%d RF_NOT_EMPTY=%d TX_EMPTY=%d RX_EMPTY=%d)\n",
				   dw_readl(dws, DW_SPI_SSIENR), sr,
				   (sr & DW_SPI_SR_BUSY) ? 1 : 0,
				   (sr & DW_SPI_SR_TF_NOT_FULL) ? 1 : 0,
				   (sr & DW_SPI_SR_RF_NOT_EMPTY) ? 1 : 0,
				   (sr & DW_SPI_SR_TX_EMPTY) ? 1 : 0,
				   (sr & DW_SPI_SR_RX_EMPTY) ? 1 : 0);
			DEBUG_PRINT("  CTRLR0=%08x BAUDR=%08x\n",
				   dw_readl(dws, DW_SPI_CTRLR0),
				   dw_readl(dws, DW_SPI_BAUDR));
			DEBUG_PRINT("  TXFLR=%08x RXFLR=%08x SER=%08x\n",
				   dw_readl(dws, DW_SPI_TXFLR),
				   dw_readl(dws, DW_SPI_RXFLR),
				   dw_readl(dws, DW_SPI_SER));
			return -1;
		}
	} while (dws->rx_len);

	return 0;
}

void dw_spi_hw_init(struct dw_spi *dws)
{
    dw_spi_reset_chip(dws);

	if (!dws->ver) {
		dws->ver = dw_readl(dws, DW_SPI_VERSION);
		DEBUG_PRINT("dws->ver = %x\n", dws->ver);
	}

	if (!dws->fifo_len) {
		uint32_t fifo;

		for (fifo = 1; fifo < 256; fifo++) {
			dw_writel(dws, DW_SPI_TXFTLR, fifo);
			if (fifo != dw_readl(dws, DW_SPI_TXFTLR))
				break;
		}
		dw_writel(dws, DW_SPI_TXFTLR, 0);

		dws->fifo_len = (fifo == 1) ? 0 : fifo;
		DEBUG_PRINT("Detected FIFO size: %u bytes\n", dws->fifo_len);
	}

	if (dw_spi_ip_is(dws, PSSI)) {
		uint32_t cr0, tmp = dw_readl(dws, DW_SPI_CTRLR0);

		dw_spi_enable_chip(dws, 0);
		dw_writel(dws, DW_SPI_CTRLR0, 0xffffffff);
		cr0 = dw_readl(dws, DW_SPI_CTRLR0);
		dw_writel(dws, DW_SPI_CTRLR0, tmp);
		dw_spi_enable_chip(dws, 1);

		if (!(cr0 & DW_PSSI_CTRLR0_DFS_MASK)) {
			dws->caps |= DW_SPI_CAP_DFS32;
			dws->dfs_offset = __bf_shf(DW_PSSI_CTRLR0_DFS32_MASK);
			DEBUG_PRINT("Detected 32-bits max data frame size\n");
		}
	} else {
		dws->caps |= DW_SPI_CAP_DFS32;
	}

	if (dws->caps & DW_SPI_CAP_CS_OVERRIDE)
		dw_writel(dws, DW_SPI_CS_OVERRIDE, 0xF);
}
