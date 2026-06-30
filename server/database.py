"""Database setup and session management."""
from sqlalchemy.ext.asyncio import AsyncSession, create_async_engine, async_sessionmaker
from sqlalchemy.orm import DeclarativeBase

from config import DATABASE_URL

engine = create_async_engine(DATABASE_URL, echo=False)
async_session = async_sessionmaker(engine, class_=AsyncSession, expire_on_commit=False)


class Base(DeclarativeBase):
    pass


async def get_db() -> AsyncSession:
    """Dependency that provides an async database session."""
    async with async_session() as session:
        try:
            yield session
        finally:
            await session.close()


async def init_db():
    """Create all tables and apply migrations."""
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
        # Migration: add device_id column for account isolation
        try:
            await conn.run_sync(
                lambda c: c.exec_driver_sql(
                    "ALTER TABLE chaoxing_accounts ADD COLUMN device_id VARCHAR(64) NOT NULL DEFAULT ''"
                )
            )
        except Exception:
            pass
        # Migration: add uid/fid/cookies_json/phone/password to share_tokens
        for col, typ in [("uid", "VARCHAR(64) DEFAULT ''"),
                         ("fid", "VARCHAR(64) DEFAULT '0'"),
                         ("cookies_json", "TEXT DEFAULT ''"),
                         ("phone", "VARCHAR(32) DEFAULT ''"),
                         ("password", "VARCHAR(64) DEFAULT ''")]:
            try:
                await conn.run_sync(
                    lambda c, col=col, typ=typ: c.exec_driver_sql(
                        f"ALTER TABLE share_tokens ADD COLUMN {col} {typ}"
                    )
                )
            except Exception:
                pass
